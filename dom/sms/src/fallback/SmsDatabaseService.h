/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_sms_MobileMessageDatabaseService_h
#define mozilla_dom_sms_MobileMessageDatabaseService_h

#include "nsISmsDatabaseService.h"
#include "mozilla/Attributes.h"

namespace mozilla {
namespace dom {
namespace sms {

class MobileMessageDatabaseService MOZ_FINAL : public nsIMobileMessageDatabaseService
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMOBILEMESSAGEDATABASESERVICE
};

} // namespace sms
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_sms_MobileMessageDatabaseService_h
