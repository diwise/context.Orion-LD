/*
*
* Copyright 2013 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Fermín Galán
*/
#include <string>
#include <vector>
#include <map>

#include "orionld/types/OrionldTenant.h"                           // OrionldTenant

#include "common/sem.h"
#include "ngsi10/UpdateContextResponse.h"
#include "mongoBackend/MongoGlobal.h"
#include "mongoBackend/MongoCommonUpdate.h"
#include "mongoBackend/mongoNotifyContext.h"



/* ****************************************************************************
*
* mongoNotifyContext -
*
* The fields "subscriptionId" and "originator" of the request are ignored,
* as we don't have anything interesting to do with them
*/
HttpStatusCode mongoNotifyContext
(
  NotifyContextRequest*            requestP,
  NotifyContextResponse*           responseP,
  OrionldTenant*                   tenantP,
  const char*                      xauthToken,
  const std::vector<std::string>&  servicePathV,
  const char*                      fiwareCorrelator,
  const std::string&               ngsiV2AttrsFormat
)
{
  bool reqSemTaken;

  reqSemTake(__FUNCTION__, "ngsi10 notification", SemWriteOp, &reqSemTaken);

  // Process each ContextElement
  for (unsigned int ix = 0; ix < requestP->contextElementResponseVector.size(); ++ix)
  {
    UpdateContextResponse               ucr;        // Unused, necessary for processContextElement()
    ContextElement*                     ceP = &requestP->contextElementResponseVector[ix]->contextElement;

    processContextElement(ceP, &ucr, ActionTypeAppend, tenantP, servicePathV, xauthToken, fiwareCorrelator, ngsiV2AttrsFormat);
  }

  reqSemGive(__FUNCTION__, "ngsi10 notification", reqSemTaken);
  responseP->responseCode.fill(SccOk);

  return SccOk;
}
