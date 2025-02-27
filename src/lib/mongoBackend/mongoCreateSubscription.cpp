/*
*
* Copyright 2016 Telefonica Investigacion y Desarrollo, S.A.U
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
* Author: Fermin Galan
*/
#include <string>
#include <vector>
#include <map>

#include "mongo/client/dbclient.h"

#include "orionld/types/OrionldTenant.h"                           // OrionldTenant

#include "logMsg/logMsg.h"
#include "logMsg/traceLevels.h"

#include "common/defaultValues.h"
#include "apiTypesV2/Subscription.h"
#include "cache/subCache.h"
#include "rest/OrionError.h"
#include "orionld/common/orionldState.h"             // orionldState

#include "mongoBackend/connectionOperations.h"
#include "mongoBackend/MongoGlobal.h"
#include "mongoBackend/MongoCommonSubscription.h"
#include "mongoBackend/dbConstants.h"
#include "mongoBackend/mongoCreateSubscription.h"



/* ****************************************************************************
*
* USING
*/
using mongo::BSONObj;
using mongo::BSONObjBuilder;
using ngsiv2::Subscription;



// -----------------------------------------------------------------------------
//
// setTimestamp -
//
static void setTimestamp(const char* name, double ts, mongo::BSONObjBuilder* bobP)
{
  bobP->append(name, ts);
}



/* ****************************************************************************
*
* insertInCache - insert in csub cache
*/
static void insertInCache
(
  const Subscription&  sub,
  const std::string&   subId,
  const std::string&   tenant,
  const std::string&   servicePath,
  bool                 notificationDone,
  double               lastNotification,
  double               lastFailure,
  double               lastSuccess
)
{
  //
  // StringFilter in Scope?
  //
  // Any Scope of type SCOPE_TYPE_SIMPLE_QUERY in sub.restriction.scopeVector?
  // If so, set it as string filter to the sub-cache item
  //
  StringFilter*  stringFilterP   = NULL;
  StringFilter*  mdStringFilterP = NULL;

  for (unsigned int ix = 0; ix < sub.restriction.scopeVector.size(); ++ix)
  {
    if (sub.restriction.scopeVector[ix]->type == SCOPE_TYPE_SIMPLE_QUERY)
    {
      stringFilterP = sub.restriction.scopeVector[ix]->stringFilterP;
    }

    if (sub.restriction.scopeVector[ix]->type == SCOPE_TYPE_SIMPLE_QUERY_MD)
    {
      mdStringFilterP = sub.restriction.scopeVector[ix]->mdStringFilterP;
    }
  }

  cacheSemTake(__FUNCTION__, "Inserting subscription in cache");
  subCacheItemInsert(tenant.c_str(),
                     servicePath.c_str(),
                     sub.notification.httpInfo,
                     sub.subject.entities,
                     sub.notification.attributes,
                     sub.notification.metadata,
                     sub.subject.condition.attributes,
                     subId.c_str(),
                     sub.expires,
                     sub.throttling,
                     sub.attrsFormat,
                     notificationDone,
                     lastNotification,
                     lastFailure,
                     lastSuccess,
                     stringFilterP,
                     mdStringFilterP,
                     sub.status,
#ifdef ORIONLD
                     sub.name,
                     sub.ldContext,
                     sub.lang,
                     sub.notification.httpInfo.mqtt.username,
                     sub.notification.httpInfo.mqtt.password,
                     sub.notification.httpInfo.mqtt.version,
                     sub.notification.httpInfo.mqtt.qos,
#endif
                     sub.subject.condition.expression.q,
                     sub.subject.condition.expression.geometry,
                     sub.subject.condition.expression.coords,
                     sub.subject.condition.expression.georel,
#ifdef ORIONLD
                     sub.subject.condition.expression.geoproperty,
#endif
                     sub.notification.blacklist);

  cacheSemGive(__FUNCTION__, "Inserting subscription in cache");
}



/* ****************************************************************************
*
* mongoCreateSubscription -
*
* Returns:
* - subId: subscription successfully created ('oe' must be ignored), the subId
*   must be used to fill Location header
* - "": subscription creation fail (look at 'oe')
*/
std::string mongoCreateSubscription
(
  const Subscription&              sub,
  OrionError*                      oe,
  OrionldTenant*                   tenantP,
  const std::vector<std::string>&  servicePathV,
  const char*                      xauthToken,
  const std::string&               fiwareCorrelator,
  const std::string&               ldContext,
  const std::string&               lang
)
{
  bool reqSemTaken = false;

  reqSemTake(__FUNCTION__, "ngsiv2 create subscription request", SemWriteOp, &reqSemTaken);

  BSONObjBuilder     b;
  std::string        servicePath      = servicePathV[0] == "" ? SERVICE_PATH_ALL : servicePathV[0];
  bool               notificationDone = false;
#ifdef ORIONLD
  const std::string  subId            = setSubscriptionId(sub, &b);
#else
  const std::string  subId            = setNewSubscriptionId(&b);
#endif

  // Build the BSON object to insert
  setExpiration(sub, &b);
  setHttpInfo(sub, &b);
  setThrottling(sub, &b);
  setServicePath(servicePath.c_str(), &b);
  setDescription(sub, &b);
  setStatus(sub, &b);
  setEntities(sub, &b);
  setAttrs(sub, &b);
  setMetadata(sub, &b);
  setBlacklist(sub, &b);

  double now = orionldState.requestTime;
  if (sub.name      != "")  setName(sub, &b);
  if (sub.ldContext != "")  setContext(sub, &b);
  if (sub.lang      != "")  setLang(sub, &b);
  if (sub.csf       != "")  setCsf(sub, &b);

  setTimestamp("createdAt",  now, &b);
  setTimestamp("modifiedAt", now, &b);

  // ---------------------------------------------------------------------------
  //
  // setTimeInterval
  //
  // setTimeInterval is not called as this tiny little call causes thousands of errors in valgrind.
  //
  // Orion-LD doesn't support periodic notifications anyway, so the value is not used.
  // Once (if) we decide that Orion-LD is to implement periodic notifications, the problems will have to be fixed.
  // The field "timeInterval" in Subscription is to be changed from 'int' to 'double' and wherever the field is used we
  // need to adapt the code to 'timeInterval' now being a 'double' and not an 'int'
  //
  // setTimeInterval(sub, &b);


  std::string status = sub.status == ""?  STATUS_ACTIVE : sub.status;

  // We need to insert the csub in the cache before (potentially) sending the
  // initial notification (have a look to issue #2974 for details)
  if (!noCache)
    insertInCache(sub, subId, tenantP->tenant, servicePath, false, 0, 0, 0);

  setCondsAndInitialNotify(sub,
                           subId,
                           status,
                           sub.notification.attributes,
                           sub.notification.metadata,
                           sub.notification.httpInfo,
                           sub.notification.blacklist,
                           sub.attrsFormat,
                           tenantP,
                           servicePathV,
                           xauthToken,
                           fiwareCorrelator,
                           &b,
                           &notificationDone);

  if (notificationDone)
  {
    double lastNotification = orionldState.requestTime;

    setLastNotification(lastNotification, &b);
    setCount(1, &b);
  }
#if 0
  else
  {
    setLastNotification(0, &b);
    setCount(0, &b);
  }
#endif

  setExpression(sub, &b);
  setFormat(sub, &b);

  BSONObj doc = b.obj();

  // Insert in DB
  std::string err;

  if (!collectionInsert(tenantP->subscriptions, doc, &err))
  {
    reqSemGive(__FUNCTION__, "ngsiv2 create subscription request", reqSemTaken);
    oe->fill(SccReceiverInternalError, err);

    return "";
  }

  reqSemGive(__FUNCTION__, "ngsiv2 create subscription request", reqSemTaken);

  return subId;
}
