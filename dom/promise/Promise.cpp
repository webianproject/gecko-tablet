/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Promise.h"

#include "jsfriendapi.h"
#include "mozilla/dom/OwningNonNull.h"
#include "mozilla/dom/PromiseBinding.h"
#include "mozilla/Preferences.h"
#include "mozilla/SyncRunnable.h"
#include "PromiseCallback.h"
#include "nsContentUtils.h"
#include "nsPIDOMWindow.h"
#include "WorkerPrivate.h"
#include "nsJSPrincipals.h"
#include "nsJSUtils.h"
#include "nsPIDOMWindow.h"
#include "nsJSEnvironment.h"

namespace mozilla {
namespace dom {

using namespace workers;

// PromiseTask

// This class processes the promise's callbacks with promise's result.
class PromiseTask MOZ_FINAL : public nsRunnable
{
public:
  PromiseTask(Promise* aPromise)
    : mPromise(aPromise)
  {
    MOZ_ASSERT(aPromise);
    MOZ_COUNT_CTOR(PromiseTask);
  }

  ~PromiseTask()
  {
    MOZ_COUNT_DTOR(PromiseTask);
  }

  NS_IMETHOD Run()
  {
    mPromise->mTaskPending = false;
    mPromise->RunTask();
    return NS_OK;
  }

private:
  nsRefPtr<Promise> mPromise;
};

class WorkerPromiseTask MOZ_FINAL : public WorkerRunnable
{
public:
  WorkerPromiseTask(WorkerPrivate* aWorkerPrivate, Promise* aPromise)
    : WorkerRunnable(aWorkerPrivate, WorkerThread,
                     UnchangedBusyCount, SkipWhenClearing)
    , mPromise(aPromise)
  {
    MOZ_ASSERT(aPromise);
    MOZ_COUNT_CTOR(WorkerPromiseTask);
  }

  ~WorkerPromiseTask()
  {
    MOZ_COUNT_DTOR(WorkerPromiseTask);
  }

  bool
  WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
  {
    mPromise->mTaskPending = false;
    mPromise->RunTask();
    return true;
  }

private:
  nsRefPtr<Promise> mPromise;
};

class PromiseResolverMixin
{
public:
  PromiseResolverMixin(Promise* aPromise,
                       JS::Handle<JS::Value> aValue,
                       Promise::PromiseState aState)
    : mPromise(aPromise)
    , mValue(aValue)
    , mState(aState)
  {
    MOZ_ASSERT(aPromise);
    MOZ_ASSERT(mState != Promise::Pending);
    MOZ_COUNT_CTOR(PromiseResolverMixin);

    JSContext* cx = nsContentUtils::GetDefaultJSContextForThread();

    /* It's safe to use unsafeGet() here: the unsafeness comes from the
     * possibility of updating the value of mJSObject without triggering the
     * barriers.  However if the value will always be marked, post barriers
     * unnecessary. */
    JS_AddNamedValueRootRT(JS_GetRuntime(cx), mValue.unsafeGet(),
                           "PromiseResolverMixin.mValue");
  }

  virtual ~PromiseResolverMixin()
  {
    NS_ASSERT_OWNINGTHREAD(PromiseResolverMixin);
    MOZ_COUNT_DTOR(PromiseResolverMixin);

    JSContext* cx = nsContentUtils::GetDefaultJSContextForThread();

    /* It's safe to use unsafeGet() here: the unsafeness comes from the
     * possibility of updating the value of mJSObject without triggering the
     * barriers.  However if the value will always be marked, post barriers
     * unnecessary. */
    JS_RemoveValueRootRT(JS_GetRuntime(cx), mValue.unsafeGet());
  }

protected:
  void
  RunInternal()
  {
    NS_ASSERT_OWNINGTHREAD(PromiseResolverMixin);
    mPromise->RunResolveTask(
      JS::Handle<JS::Value>::fromMarkedLocation(mValue.address()),
      mState, Promise::SyncTask);
  }

private:
  nsRefPtr<Promise> mPromise;
  JS::Heap<JS::Value> mValue;
  Promise::PromiseState mState;
  NS_DECL_OWNINGTHREAD;
};

// This class processes the promise's callbacks with promise's result.
class PromiseResolverTask MOZ_FINAL : public nsRunnable,
                                      public PromiseResolverMixin
{
public:
  PromiseResolverTask(Promise* aPromise,
                      JS::Handle<JS::Value> aValue,
                      Promise::PromiseState aState)
    : PromiseResolverMixin(aPromise, aValue, aState)
  {}

  ~PromiseResolverTask()
  {}

  NS_IMETHOD Run()
  {
    RunInternal();
    return NS_OK;
  }
};

class WorkerPromiseResolverTask MOZ_FINAL : public WorkerRunnable,
                                            public PromiseResolverMixin
{
public:
  WorkerPromiseResolverTask(WorkerPrivate* aWorkerPrivate,
                            Promise* aPromise,
                            JS::Handle<JS::Value> aValue,
                            Promise::PromiseState aState)
    : WorkerRunnable(aWorkerPrivate, WorkerThread,
                     UnchangedBusyCount, SkipWhenClearing),
      PromiseResolverMixin(aPromise, aValue, aState)
  {}

  ~WorkerPromiseResolverTask()
  {}

  bool
  WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
  {
    RunInternal();
    return true;
  }
};

// Promise

NS_IMPL_CYCLE_COLLECTION_CLASS(Promise)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Promise)
  tmp->MaybeReportRejected();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mResolveCallbacks);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRejectCallbacks);
  tmp->mResult = JS::UndefinedValue();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Promise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResolveCallbacks);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRejectCallbacks);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(Promise)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JSVAL_MEMBER_CALLBACK(mResult)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Promise)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Promise)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Promise)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

Promise::Promise(nsPIDOMWindow* aWindow)
  : mWindow(aWindow)
  , mResult(JS::UndefinedValue())
  , mState(Pending)
  , mTaskPending(false)
  , mHadRejectCallback(false)
  , mResolvePending(false)
{
  MOZ_COUNT_CTOR(Promise);
  mozilla::HoldJSObjects(this);
  SetIsDOMBinding();
}

Promise::~Promise()
{
  MaybeReportRejected();
  mResult = JS::UndefinedValue();
  mozilla::DropJSObjects(this);
  MOZ_COUNT_DTOR(Promise);
}

JSObject*
Promise::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aScope)
{
  return PromiseBinding::Wrap(aCx, aScope, this);
}

class PromisePrefEnabledRunnable : public nsRunnable
{
public:
  PromisePrefEnabledRunnable() : mEnabled(false) {}

  bool
  Enabled()
  {
    return mEnabled;
  }

  NS_IMETHODIMP
  Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    mEnabled = Preferences::GetBool("dom.promise.enabled", false);
    return NS_OK;
  }

private:
  bool mEnabled;
};

/* static */ bool
Promise::EnabledForScope(JSContext* aCx, JSObject* /* unused */)
{
  // Enable if the pref is enabled or if we're chrome or if we're a
  // certified app.
  nsCOMPtr<nsIThread> mainThread;
  nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
  NS_ENSURE_SUCCESS(rv, false);

  nsRefPtr<PromisePrefEnabledRunnable> r = new PromisePrefEnabledRunnable();

  // When used from the main thread, SyncRunnable will internally directly call
  // the function rather than dispatch a Runnable. So this is usable on any
  // thread.
  // Although this pause is expensive, it is performed only once per worker when
  // the worker in initialized.
  SyncRunnable::DispatchToThread(mainThread, r);
  if (r->Enabled()) {
    return true;
  }

  // FIXME(nsm): Remove these checks once promises are enabled by default.
  // Note that we have no concept of a certified app in workers.
  // XXXbz well, why not?
  if (!NS_IsMainThread()) {
    return GetWorkerPrivateFromContext(aCx)->IsChromeWorker();
  }

  nsIPrincipal* prin = nsContentUtils::GetSubjectPrincipal();
  return nsContentUtils::IsSystemPrincipal(prin) ||
    prin->GetAppStatus() == nsIPrincipal::APP_STATUS_CERTIFIED;
}

void
Promise::MaybeResolve(JSContext* aCx,
                      const Optional<JS::Handle<JS::Value> >& aValue)
{
  MaybeResolveInternal(aCx, aValue);
}

void
Promise::MaybeReject(JSContext* aCx,
                     const Optional<JS::Handle<JS::Value> >& aValue)
{
  MaybeRejectInternal(aCx, aValue);
}

static void
EnterCompartment(Maybe<JSAutoCompartment>& aAc, JSContext* aCx,
                 const Optional<JS::Handle<JS::Value> >& aValue)
{
  // FIXME Bug 878849
  if (aValue.WasPassed() && aValue.Value().isObject()) {
    JS::Rooted<JSObject*> rooted(aCx, &aValue.Value().toObject());
    aAc.construct(aCx, rooted);
  }
}

enum {
  SLOT_PROMISE = 0,
  SLOT_TASK
};

/* static */ bool
Promise::JSCallback(JSContext *aCx, unsigned aArgc, JS::Value *aVp)
{
  JS::CallArgs args = CallArgsFromVp(aArgc, aVp);

  JS::Rooted<JS::Value> v(aCx,
                          js::GetFunctionNativeReserved(&args.callee(),
                                                        SLOT_PROMISE));
  MOZ_ASSERT(v.isObject());

  Promise* promise;
  if (NS_FAILED(UNWRAP_OBJECT(Promise, aCx, &v.toObject(), promise))) {
    return Throw(aCx, NS_ERROR_UNEXPECTED);
  }

  Optional<JS::Handle<JS::Value> > value(aCx);
  if (aArgc) {
    value.Value() = args[0];
  }

  v = js::GetFunctionNativeReserved(&args.callee(), SLOT_TASK);
  PromiseCallback::Task task = static_cast<PromiseCallback::Task>(v.toInt32());

  if (task == PromiseCallback::Resolve) {
    promise->MaybeResolveInternal(aCx, value);
  } else {
    promise->MaybeRejectInternal(aCx, value);
  }

  return true;
}

/* static */ JSObject*
Promise::CreateFunction(JSContext* aCx, JSObject* aParent, Promise* aPromise,
                        int32_t aTask)
{
  JSFunction* func = js::NewFunctionWithReserved(aCx, JSCallback,
                                                 1 /* nargs */, 0 /* flags */,
                                                 aParent, nullptr);
  if (!func) {
    return nullptr;
  }

  JS::Rooted<JSObject*> obj(aCx, JS_GetFunctionObject(func));

  JS::Rooted<JS::Value> promiseObj(aCx);
  if (!dom::WrapNewBindingObject(aCx, obj, aPromise, &promiseObj)) {
    return nullptr;
  }

  js::SetFunctionNativeReserved(obj, SLOT_PROMISE, promiseObj);
  js::SetFunctionNativeReserved(obj, SLOT_TASK, JS::Int32Value(aTask));

  return obj;
}

/* static */ already_AddRefed<Promise>
Promise::Constructor(const GlobalObject& aGlobal,
                     PromiseInit& aInit, ErrorResult& aRv)
{
  JSContext* cx = aGlobal.GetContext();
  nsCOMPtr<nsPIDOMWindow> window;

  // On workers, let the window be null.
  if (MOZ_LIKELY(NS_IsMainThread())) {
    window = do_QueryInterface(aGlobal.GetAsSupports());
    if (!window) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
  }

  nsRefPtr<Promise> promise = new Promise(window);

  JS::Rooted<JSObject*> resolveFunc(cx,
                                    CreateFunction(cx, aGlobal.Get(), promise,
                                                   PromiseCallback::Resolve));
  if (!resolveFunc) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  JS::Rooted<JSObject*> rejectFunc(cx,
                                   CreateFunction(cx, aGlobal.Get(), promise,
                                                  PromiseCallback::Reject));
  if (!rejectFunc) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  aInit.Call(promise, resolveFunc, rejectFunc, aRv,
             CallbackObject::eRethrowExceptions);
  aRv.WouldReportJSException();

  if (aRv.IsJSException()) {
    Optional<JS::Handle<JS::Value> > value(cx);
    aRv.StealJSException(cx, &value.Value());

    Maybe<JSAutoCompartment> ac;
    EnterCompartment(ac, cx, value);
    promise->MaybeRejectInternal(cx, value);
  }

  return promise.forget();
}

/* static */ already_AddRefed<Promise>
Promise::Resolve(const GlobalObject& aGlobal, JSContext* aCx,
                 JS::Handle<JS::Value> aValue, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> window;
  if (MOZ_LIKELY(NS_IsMainThread())) {
    window = do_QueryInterface(aGlobal.GetAsSupports());
    if (!window) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
  }

  nsRefPtr<Promise> promise = new Promise(window);

  Optional<JS::Handle<JS::Value> > value(aCx, aValue);
  promise->MaybeResolveInternal(aCx, value);
  return promise.forget();
}

/* static */ already_AddRefed<Promise>
Promise::Reject(const GlobalObject& aGlobal, JSContext* aCx,
                JS::Handle<JS::Value> aValue, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> window;
  if (MOZ_LIKELY(NS_IsMainThread())) {
    window = do_QueryInterface(aGlobal.GetAsSupports());
    if (!window) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
  }

  nsRefPtr<Promise> promise = new Promise(window);

  Optional<JS::Handle<JS::Value> > value(aCx, aValue);
  promise->MaybeRejectInternal(aCx, value);
  return promise.forget();
}

already_AddRefed<Promise>
Promise::Then(const Optional<OwningNonNull<AnyCallback> >& aResolveCallback,
              const Optional<OwningNonNull<AnyCallback> >& aRejectCallback)
{
  nsRefPtr<Promise> promise = new Promise(GetParentObject());

  nsRefPtr<PromiseCallback> resolveCb =
    PromiseCallback::Factory(promise,
                             aResolveCallback.WasPassed()
                               ? &aResolveCallback.Value()
                               : nullptr,
                             PromiseCallback::Resolve);

  nsRefPtr<PromiseCallback> rejectCb =
    PromiseCallback::Factory(promise,
                             aRejectCallback.WasPassed()
                               ? &aRejectCallback.Value()
                               : nullptr,
                             PromiseCallback::Reject);

  AppendCallbacks(resolveCb, rejectCb);

  return promise.forget();
}

already_AddRefed<Promise>
Promise::Catch(const Optional<OwningNonNull<AnyCallback> >& aRejectCallback)
{
  Optional<OwningNonNull<AnyCallback> > resolveCb;
  return Then(resolveCb, aRejectCallback);
}

void
Promise::AppendCallbacks(PromiseCallback* aResolveCallback,
                         PromiseCallback* aRejectCallback)
{
  if (aResolveCallback) {
    mResolveCallbacks.AppendElement(aResolveCallback);
  }

  if (aRejectCallback) {
    mHadRejectCallback = true;
    mRejectCallbacks.AppendElement(aRejectCallback);
  }

  // If promise's state is resolved, queue a task to process our resolve
  // callbacks with promise's result. If promise's state is rejected, queue a
  // task to process our reject callbacks with promise's result.
  if (mState != Pending && !mTaskPending) {
    if (MOZ_LIKELY(NS_IsMainThread())) {
      nsRefPtr<PromiseTask> task = new PromiseTask(this);
      NS_DispatchToCurrentThread(task);
    } else {
      WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
      MOZ_ASSERT(worker);
      nsRefPtr<WorkerPromiseTask> task = new WorkerPromiseTask(worker, this);
      worker->Dispatch(task);
    }
    mTaskPending = true;
  }
}

void
Promise::RunTask()
{
  MOZ_ASSERT(mState != Pending);

  nsTArray<nsRefPtr<PromiseCallback> > callbacks;
  callbacks.SwapElements(mState == Resolved ? mResolveCallbacks
                                            : mRejectCallbacks);
  mResolveCallbacks.Clear();
  mRejectCallbacks.Clear();

  JSContext* cx = nsContentUtils::GetDefaultJSContextForThread();
  JSAutoRequest ar(cx);
  Optional<JS::Handle<JS::Value> > value(cx, mResult);

  for (uint32_t i = 0; i < callbacks.Length(); ++i) {
    callbacks[i]->Call(value);
  }
}

void
Promise::MaybeReportRejected()
{
  if (mState != Rejected || mHadRejectCallback || mResult.isUndefined()) {
    return;
  }

  JSErrorReport* report = js::ErrorFromException(mResult);
  if (!report) {
    return;
  }

  MOZ_ASSERT(mResult.isObject(), "How did we get a JSErrorReport?");

  // Remains null in case of worker.
  nsCOMPtr<nsPIDOMWindow> win;
  bool isChromeError = false;

  if (MOZ_LIKELY(NS_IsMainThread())) {
    win =
      do_QueryInterface(nsJSUtils::GetStaticScriptGlobal(&mResult.toObject()));
    nsIPrincipal* principal = nsContentUtils::GetObjectPrincipal(&mResult.toObject());
    isChromeError = nsContentUtils::IsSystemPrincipal(principal);
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(worker);
    isChromeError = worker->IsChromeWorker();
  }

  // Now post an event to do the real reporting async
  NS_DispatchToMainThread(
    new AsyncErrorReporter(JS_GetObjectRuntime(&mResult.toObject()),
                           report,
                           nullptr,
                           isChromeError,
                           win));
}

void
Promise::MaybeResolveInternal(JSContext* aCx,
                              const Optional<JS::Handle<JS::Value> >& aValue,
                              PromiseTaskSync aAsynchronous)
{
  if (mResolvePending) {
    return;
  }

  ResolveInternal(aCx, aValue, aAsynchronous);
}

void
Promise::MaybeRejectInternal(JSContext* aCx,
                             const Optional<JS::Handle<JS::Value> >& aValue,
                             PromiseTaskSync aAsynchronous)
{
  if (mResolvePending) {
    return;
  }

  RejectInternal(aCx, aValue, aAsynchronous);
}

void
Promise::ResolveInternal(JSContext* aCx,
                         const Optional<JS::Handle<JS::Value> >& aValue,
                         PromiseTaskSync aAsynchronous)
{
  mResolvePending = true;

  // TODO: Bug 879245 - Then-able objects
  if (aValue.WasPassed() && aValue.Value().isObject()) {
    JS::Rooted<JSObject*> valueObj(aCx, &aValue.Value().toObject());
    Promise* nextPromise;
    nsresult rv = UNWRAP_OBJECT(Promise, aCx, valueObj, nextPromise);

    if (NS_SUCCEEDED(rv)) {
      nsRefPtr<PromiseCallback> resolveCb = new ResolvePromiseCallback(this);
      nsRefPtr<PromiseCallback> rejectCb = new RejectPromiseCallback(this);
      nextPromise->AppendCallbacks(resolveCb, rejectCb);
      return;
    }
  }

  // If the synchronous flag is set, process our resolve callbacks with
  // value. Otherwise, the synchronous flag is unset, queue a task to process
  // own resolve callbacks with value. Otherwise, the synchronous flag is
  // unset, queue a task to process our resolve callbacks with value.
  RunResolveTask(aValue.WasPassed() ? aValue.Value() : JS::UndefinedHandleValue,
                 Resolved, aAsynchronous);
}

void
Promise::RejectInternal(JSContext* aCx,
                        const Optional<JS::Handle<JS::Value> >& aValue,
                        PromiseTaskSync aAsynchronous)
{
  mResolvePending = true;

  // If the synchronous flag is set, process our reject callbacks with
  // value. Otherwise, the synchronous flag is unset, queue a task to process
  // promise's reject callbacks with value.
  RunResolveTask(aValue.WasPassed() ? aValue.Value() : JS::UndefinedHandleValue,
                 Rejected, aAsynchronous);
}

void
Promise::RunResolveTask(JS::Handle<JS::Value> aValue,
                        PromiseState aState,
                        PromiseTaskSync aAsynchronous)
{
  // If the synchronous flag is unset, queue a task to process our
  // accept callbacks with value.
  if (aAsynchronous == AsyncTask) {
    if (MOZ_LIKELY(NS_IsMainThread())) {
      nsRefPtr<PromiseResolverTask> task =
        new PromiseResolverTask(this, aValue, aState);
      NS_DispatchToCurrentThread(task);
    } else {
      WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
      MOZ_ASSERT(worker);
      nsRefPtr<WorkerPromiseResolverTask> task =
        new WorkerPromiseResolverTask(worker, this, aValue, aState);
      worker->Dispatch(task);
    }
    return;
  }

  SetResult(aValue);
  SetState(aState);
  RunTask();
}

} // namespace dom
} // namespace mozilla
