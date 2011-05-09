/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

#include "storage_test_harness.h"
#include "prthread.h"
#include "nsIEventTarget.h"
#include "nsIInterfaceRequestorUtils.h"

#include "sqlite3.h"

////////////////////////////////////////////////////////////////////////////////
//// Async Helpers

/**
 * Invoke AsyncClose on the given connection, blocking the main thread until we
 * get the completion notification.
 */
void
blocking_async_close(mozIStorageConnection *db)
{
  nsRefPtr<AsyncStatementSpinner> spinner(new AsyncStatementSpinner());

  db->AsyncClose(spinner);
  spinner->SpinUntilCompleted();
}

/**
 * Spins the events loop for current thread until aCondition is true.
 */
void
spin_events_loop_until_true(const bool* const aCondition)
{
  nsCOMPtr<nsIThread> thread(::do_GetCurrentThread());
  nsresult rv = NS_OK;
  PRBool processed = PR_TRUE;
  while (!(*aCondition) && NS_SUCCEEDED(rv)) {
    rv = thread->ProcessNextEvent(true, &processed);
  }
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageStatementCallback implementation

class UnownedCallback : public mozIStorageStatementCallback
{
public:
  NS_DECL_ISUPPORTS

  // Whether the object has been destroyed.
  static bool sAlive;
  // Whether the first result was received.
  static bool sResult;
  // Whether an error was received.
  static bool sError;

  UnownedCallback(mozIStorageConnection* aDBConn)
  : mDBConn(aDBConn)
  , mCompleted(false)
  {
    sAlive = true;
    sResult = false;
    sError = false;
  }

  ~UnownedCallback()
  {
    sAlive = false;
    blocking_async_close(mDBConn);
  }

  NS_IMETHOD HandleResult(mozIStorageResultSet* aResultSet)
  {
    sResult = true;
    spin_events_loop_until_true(&mCompleted);
    if (!sAlive) {
      NS_RUNTIMEABORT("The statement callback was destroyed prematurely.");
    }
    return NS_OK;
  }

  NS_IMETHOD HandleError(mozIStorageError* aError)
  {
    sError = true;
    spin_events_loop_until_true(&mCompleted);
    if (!sAlive) {
      NS_RUNTIMEABORT("The statement callback was destroyed prematurely.");
    }
    return NS_OK;
  }

  NS_IMETHOD HandleCompletion(PRUint16 aReason)
  {
    mCompleted = true;
    return NS_OK;
  }

protected:
  nsCOMPtr<mozIStorageConnection> mDBConn;
  bool mCompleted;
};

NS_IMPL_ISUPPORTS1(UnownedCallback, mozIStorageStatementCallback)

bool UnownedCallback::sAlive = false;
bool UnownedCallback::sResult = false;
bool UnownedCallback::sError = false;

////////////////////////////////////////////////////////////////////////////////
//// Tests

void
test_SpinEventsLoopInHandleResult()
{
  nsCOMPtr<mozIStorageConnection> db(getMemoryDatabase());

  // Create a test table and populate it.
  nsCOMPtr<mozIStorageStatement> stmt;
  db->CreateStatement(NS_LITERAL_CSTRING(
    "CREATE TABLE test (id INTEGER PRIMARY KEY)"
  ), getter_AddRefs(stmt));
  stmt->Execute();
  stmt->Finalize();

  db->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO test (id) VALUES (?)"
  ), getter_AddRefs(stmt));
  for (PRInt32 i = 0; i < 30; ++i) {
    stmt->BindInt32ByIndex(0, i);
    stmt->Execute();
    stmt->Reset();
  }
  stmt->Finalize();

  db->CreateStatement(NS_LITERAL_CSTRING(
    "SELECT * FROM test"
  ), getter_AddRefs(stmt));
  nsCOMPtr<mozIStoragePendingStatement> ps;
  do_check_success(stmt->ExecuteAsync(new UnownedCallback(db),
                                      getter_AddRefs(ps)));
  stmt->Finalize();

  spin_events_loop_until_true(&UnownedCallback::sResult);
}

void
test_SpinEventsLoopInHandleError()
{
  nsCOMPtr<mozIStorageConnection> db(getMemoryDatabase());

  // Create a test table and populate it.
  nsCOMPtr<mozIStorageStatement> stmt;
  db->CreateStatement(NS_LITERAL_CSTRING(
    "CREATE TABLE test (id INTEGER PRIMARY KEY)"
  ), getter_AddRefs(stmt));
  stmt->Execute();
  stmt->Finalize();

  db->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO test (id) VALUES (1)"
  ), getter_AddRefs(stmt));
  stmt->Execute();
  stmt->Finalize();

  // This will cause a constraint error.
  db->CreateStatement(NS_LITERAL_CSTRING(
    "INSERT INTO test (id) VALUES (1)"
  ), getter_AddRefs(stmt));
  nsCOMPtr<mozIStoragePendingStatement> ps;
  do_check_success(stmt->ExecuteAsync(new UnownedCallback(db),
                                      getter_AddRefs(ps)));
  stmt->Finalize();

  spin_events_loop_until_true(&UnownedCallback::sError);
}

void (*gTests[])(void) = {
  test_SpinEventsLoopInHandleResult,
  test_SpinEventsLoopInHandleError,
};

const char *file = __FILE__;
#define TEST_NAME "test async callbacks with spun event loops"
#define TEST_FILE file
#include "storage_test_harness_tail.h"
