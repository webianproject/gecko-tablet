/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NS_SMILINSTANCETIME_H_
#define NS_SMILINSTANCETIME_H_

#include "nsSMILTimeValue.h"
#include "nsAutoPtr.h"

class nsSMILInterval;
class nsSMILTimeContainer;
class nsSMILTimeValueSpec;

//----------------------------------------------------------------------
// nsSMILInstanceTime
//
// An instant in document simple time that may be used in creating a new
// interval.
//
// For an overview of how this class is related to other SMIL time classes see
// the documentation in nsSMILTimeValue.h
//
// These objects are owned by an nsSMILTimedElement but MAY also be referenced
// by:
//
// a) nsSMILIntervals that belong to the same nsSMILTimedElement and which refer
//    to the nsSMILInstanceTimes which form the interval endpoints; and/or
// b) nsSMILIntervals that belong to other nsSMILTimedElements but which need to
//    update dependent instance times when they change or are deleted.
//    E.g. for begin='a.begin', 'a' needs to inform dependent
//    nsSMILInstanceTimes if its begin time changes. This notification is
//    performed by the nsSMILInterval.

class nsSMILInstanceTime
{
public:
  // Instance time source. Times generated by events, syncbase relationships,
  // and DOM calls behave differently in some circumstances such as when a timed
  // element is reset.
  enum nsSMILInstanceTimeSource {
    // No particularly significant source, e.g. offset time, 'indefinite'
    SOURCE_NONE,
    // Generated by a DOM call such as beginElement
    SOURCE_DOM,
    // Generated by a syncbase relationship
    SOURCE_SYNCBASE,
    // Generated by an event
    SOURCE_EVENT
  };

  nsSMILInstanceTime(const nsSMILTimeValue& aTime,
                     nsSMILInstanceTimeSource aSource = SOURCE_NONE,
                     nsSMILTimeValueSpec* aCreator = nullptr,
                     nsSMILInterval* aBaseInterval = nullptr);
  ~nsSMILInstanceTime();
  void Unlink();
  void HandleChangedInterval(const nsSMILTimeContainer* aSrcContainer,
                             bool aBeginObjectChanged,
                             bool aEndObjectChanged);
  void HandleDeletedInterval();
  void HandleFilteredInterval();

  const nsSMILTimeValue& Time() const { return mTime; }
  const nsSMILTimeValueSpec* GetCreator() const { return mCreator; }

  bool IsDynamic() const { return !!(mFlags & kDynamic); }
  bool IsFixedTime() const { return !(mFlags & kMayUpdate); }
  bool FromDOM() const { return !!(mFlags & kFromDOM); }

  bool ShouldPreserve() const;
  void   UnmarkShouldPreserve();

  void AddRefFixedEndpoint();
  void ReleaseFixedEndpoint();

  void DependentUpdate(const nsSMILTimeValue& aNewTime)
  {
    NS_ABORT_IF_FALSE(!IsFixedTime(),
        "Updating an instance time that is not expected to be updated");
    mTime = aNewTime;
  }

  bool IsDependent() const { return !!mBaseInterval; }
  bool IsDependentOn(const nsSMILInstanceTime& aOther) const;
  const nsSMILInterval* GetBaseInterval() const { return mBaseInterval; }
  const nsSMILInstanceTime* GetBaseTime() const;

  bool SameTimeAndBase(const nsSMILInstanceTime& aOther) const
  {
    return mTime == aOther.mTime && GetBaseTime() == aOther.GetBaseTime();
  }

  // Get and set a serial number which may be used by a containing class to
  // control the sort order of otherwise similar instance times.
  uint32_t Serial() const { return mSerial; }
  void SetSerial(uint32_t aIndex) { mSerial = aIndex; }

  NS_INLINE_DECL_REFCOUNTING(nsSMILInstanceTime)

protected:
  void SetBaseInterval(nsSMILInterval* aBaseInterval);

  nsSMILTimeValue mTime;

  // Internal flags used to represent the behaviour of different instance times
  enum {
    // Indicates that this instance time was generated by an event or a DOM
    // call. Such instance times require special handling when (i) the owning
    // element is reset, (ii) when they are to be added as a new end instance
    // times (as per SMIL's event sensitivity contraints), and (iii) when
    // a backwards seek is performed and the timing model is reconstructed.
    kDynamic = 1,

    // Indicates that this instance time is referred to by an
    // nsSMILTimeValueSpec and as such may be updated. Such instance time should
    // not be filtered out by the nsSMILTimedElement even if they appear to be
    // in the past as they may be updated to a future time.
    kMayUpdate = 2,

    // Indicates that this instance time was generated from the DOM as opposed
    // to an nsSMILTimeValueSpec. When a 'begin' or 'end' attribute is set or
    // reset we should clear all the instance times that have been generated by
    // that attribute (and hence an nsSMILTimeValueSpec), but not those from the
    // DOM.
    kFromDOM = 4,

    // Indicates that this instance time was used as the endpoint of an interval
    // that has been filtered or removed. However, since it is a dynamic time it
    // should be preserved and not filtered.
    kWasDynamicEndpoint = 8
  };
  uint8_t       mFlags;   // Combination of kDynamic, kMayUpdate, etc.
  bool          mVisited; // (mutable) Cycle tracking

  // Additional reference count to determine if this instance time is currently
  // used as a fixed endpoint in any intervals. Instance times that are used in
  // this way should not be removed when the owning nsSMILTimedElement removes
  // instance times in response to a restart or in an attempt to free up memory
  // by filtering out old instance times.
  //
  // Instance times are only shared in a few cases, namely:
  // a) early ends,
  // b) zero-duration intervals,
  // c) momentarily whilst establishing new intervals and updating the current
  //    interval, and
  // d) trimmed intervals
  // Hence the limited range of a uint16_t should be more than adequate.
  uint16_t      mFixedEndpointRefCnt;

  uint32_t      mSerial; // A serial number used by the containing class to
                         // specify the sort order for instance times with the
                         // same mTime.

  nsSMILTimeValueSpec* mCreator; // The nsSMILTimeValueSpec object that created
                                 // us. (currently only needed for syncbase
                                 // instance times.)
  nsSMILInterval* mBaseInterval; // Interval from which this time is derived
                                 // (only used for syncbase instance times)
};

#endif // NS_SMILINSTANCETIME_H_
