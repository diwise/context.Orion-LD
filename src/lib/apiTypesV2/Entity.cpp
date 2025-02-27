/*
*
* Copyright 2015 Telefonica Investigacion y Desarrollo, S.A.U
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
* Author: Ken Zangelin
*/
#include <string>
#include <vector>
#include <map>

#include "logMsg/traceLevels.h"
#include "logMsg/logMsg.h"

#include "orionld/common/orionldState.h"

#include "common/tag.h"
#include "common/string.h"
#include "common/globals.h"
#include "common/errorMessages.h"
#include "rest/uriParamNames.h"
#include "alarmMgr/alarmMgr.h"
#include "parse/forbiddenChars.h"
#include "ngsi10/QueryContextResponse.h"

#include "apiTypesV2/Entity.h"



/* ****************************************************************************
*
* Entity::Entity - 
*/
Entity::Entity(): isTypePattern(false), typeGiven(false), renderId(true), creDate(0), modDate(0)
{
}



/* ****************************************************************************
*
* Entity::~Entity - 
*/
Entity::~Entity()
{
  release();
}



/* ****************************************************************************
*
* Entity::render - 
*
* The rendering of JSON in APIv2 depends on the URI param 'options'
* Rendering methods:
*   o 'normalized' (default)
*   o 'keyValues'  (less verbose, only name and values shown for attributes - no type, no metadatas)
*   o 'values'     (only the values of the attributes are printed, in a vector)
*/
std::string Entity::render(bool comma)
{
  if ((oe.details != "") || ((oe.reasonPhrase != "OK") && (oe.reasonPhrase != "")))
  {
    return oe.toJson();
  }

  RenderFormat  renderFormat = RF_NORMALIZED;

  if      (orionldState.uriParamOptions.keyValues    == true)  { renderFormat = RF_KEYVALUES;     }
  else if (orionldState.uriParamOptions.values       == true)  { renderFormat = RF_VALUES;        }
  else if (orionldState.uriParamOptions.uniqueValues == true)  { renderFormat = RF_UNIQUE_VALUES; }

  std::string               out;
  std::vector<std::string>  metadataFilter;
  std::vector<std::string>  attrsFilter;

  if (orionldState.uriParams.metadata != NULL)
    stringSplit(orionldState.uriParams.metadata, ',', metadataFilter);

  if (orionldState.uriParams.attrs != NULL)
    stringSplit(orionldState.uriParams.attrs, ',', attrsFilter);


  // Add special attributes representing entity dates
  if ((creDate != 0) && ((orionldState.uriParamOptions.dateCreated == true) || (std::find(attrsFilter.begin(), attrsFilter.end(), DATE_CREATED) != attrsFilter.end())))
  {
    ContextAttribute* creDateAttrP = attributeVector.lookup(DATE_CREATED);

    if (creDateAttrP == NULL)
    {
      // If not found - create it
      ContextAttribute* caP = new ContextAttribute(DATE_CREATED, DATE_TYPE, creDate);
      attributeVector.push_back(caP);
    }
    else
    {
      // If found - modify it?
      // creDateAttrP->numberValue = creDate;
    }
  }
  if ((modDate != 0) && ((orionldState.uriParamOptions.dateModified == true) || (std::find(attrsFilter.begin(), attrsFilter.end(), DATE_MODIFIED) != attrsFilter.end())))
  {
    ContextAttribute* modDateAttrP = attributeVector.lookup(DATE_MODIFIED);

    if (modDateAttrP == NULL)
    {
      // If not found - create it
      ContextAttribute* caP = new ContextAttribute(DATE_MODIFIED, DATE_TYPE, modDate);
      attributeVector.push_back(caP);
    }
    else
    {
      // If found - modify it?
      // modDateAttrP->numberValue = modDate;
    }
  }

  if ((renderFormat == RF_VALUES) || (renderFormat == RF_UNIQUE_VALUES))
  {
    out = "[";
    if (attributeVector.size() != 0)
    {
      out += attributeVector.toJson(renderFormat, attrsFilter, metadataFilter, false);
    }
    out += "]";
  }
  else
  {
    out = "{";

    if (renderId)
    {
      out += JSON_VALUE("id", id);
      out += ",";

      /* This is needed for entities coming from NGSIv1 (which allows empty or missing types) */
      out += JSON_STR("type") + ":" + ((type != "")? JSON_STR(type) : JSON_STR(DEFAULT_ENTITY_TYPE));
    }

    std::string attrsOut;
    if (attributeVector.size() != 0)
    {
      attrsOut += attributeVector.toJson(renderFormat, attrsFilter, metadataFilter, false);
    }

    //
    // Note that just attributeVector.size() != 0 (used in previous versions) cannot be used
    // as uri-param "attrs" could remove all of the attributes
    //
    if (attrsOut != "")
    {
      if (renderId)
      {
        out +=  "," + attrsOut;
      }
      else
      {
        out += attrsOut;
      }
    }

    out += "}";
  }

  if (comma)
  {
    out += ",";
  }

  return out;
}



/* ****************************************************************************
*
* Entity::check - 
*/
std::string Entity::check(RequestType requestType)
{
  ssize_t  len;
  char     errorMsg[128];

  if (((len = strlen(id.c_str())) < MIN_ID_LEN) && (requestType != EntityRequest))
  {
    snprintf(errorMsg, sizeof errorMsg, "entity id length: %zd, min length supported: %d", len, MIN_ID_LEN);
    alarmMgr.badInput(clientIp, errorMsg);
    return std::string(errorMsg);
  }

  if ((requestType == EntitiesRequest) && (id.empty()))
  {
    return "No Entity ID";
  }

  if ( (len = strlen(id.c_str())) > MAX_ID_LEN)
  {
    snprintf(errorMsg, sizeof errorMsg, "entity id length: %zd, max length supported: %d", len, MAX_ID_LEN);
    alarmMgr.badInput(clientIp, errorMsg);
    return std::string(errorMsg);
  }

  if (isPattern.empty())
  {
    isPattern = "false";
  }

  // isPattern MUST be either "true" or "false" (or empty => "false")
  if ((isPattern != "true") && (isPattern != "false"))
  {
    alarmMgr.badInput(clientIp, "invalid value for isPattern");
    return "Invalid value for isPattern";
  }

  // Check for forbidden chars for "id", but not if "id" is a pattern
  if (isPattern == "false")
  {
    if (forbiddenIdChars(V2, id.c_str()))
    {
      alarmMgr.badInput(clientIp, ERROR_DESC_BAD_REQUEST_INVALID_CHAR_ENTID);
      return ERROR_DESC_BAD_REQUEST_INVALID_CHAR_ENTID;
    }
  }

  if ( (len = strlen(type.c_str())) > MAX_ID_LEN)
  {
    snprintf(errorMsg, sizeof errorMsg, "entity type length: %zd, max length supported: %d", len, MAX_ID_LEN);
    alarmMgr.badInput(clientIp, errorMsg);
    return std::string(errorMsg);
  }


  if (!((requestType == BatchQueryRequest) || (requestType == BatchUpdateRequest && !typeGiven)))
  {
    if ( (len = strlen(type.c_str())) < MIN_ID_LEN)
    {
      snprintf(errorMsg, sizeof errorMsg, "entity type length: %zd, min length supported: %d", len, MIN_ID_LEN);
      alarmMgr.badInput(clientIp, errorMsg);
      return std::string(errorMsg);
    }
  }

  // Check for forbidden chars for "type", but not if "type" is a pattern
  if (isTypePattern == false)
  {
    if (forbiddenIdChars(V2, type.c_str()))
    {
      alarmMgr.badInput(clientIp, ERROR_DESC_BAD_REQUEST_INVALID_CHAR_ENTTYPE);
      return ERROR_DESC_BAD_REQUEST_INVALID_CHAR_ENTTYPE;
    }
  }

  return attributeVector.check(V2, requestType);
}



/* ****************************************************************************
*
* Entity::fill - 
*/
void Entity::fill
(
  const std::string&       _id,
  const std::string&       _type,
  const std::string&       _isPattern,
  ContextAttributeVector*  aVec,
  double                   _creDate,
  double                   _modDate
)
{
  id         = _id;
  type       = _type;
  isPattern  = _isPattern;

  creDate    = _creDate;
  modDate    = _modDate;

  attributeVector.fill(aVec);
}



/* ****************************************************************************
*
* Entity::fill -
*/
void Entity::fill(QueryContextResponse* qcrsP)
{
  if (qcrsP->errorCode.code == SccContextElementNotFound)
  {
    oe.fill(SccContextElementNotFound, ERROR_DESC_NOT_FOUND_ENTITY, ERROR_NOT_FOUND);
  }
  else if (qcrsP->errorCode.code != SccOk)
  {
    //
    // any other error distinct from Not Found
    //
    oe.fill(qcrsP->errorCode.code, qcrsP->errorCode.details, qcrsP->errorCode.reasonPhrase);
  }
  else if (qcrsP->contextElementResponseVector.size() > 1)  // qcrsP->errorCode.code == SccOk
  {
    //
    // If there are more than one entity, we return an error
    //
    oe.fill(SccConflict, ERROR_DESC_TOO_MANY_ENTITIES, ERROR_TOO_MANY);
  }
  else
  {
    ContextElement* ceP = &qcrsP->contextElementResponseVector[0]->contextElement;

    fill(ceP->entityId.id,
         ceP->entityId.type,
         ceP->entityId.isPattern,
         &ceP->contextAttributeVector,
         ceP->entityId.creDate,
         ceP->entityId.modDate);
  }
}



/* ****************************************************************************
*
* Entity::release - 
*/
void Entity::release(void)
{
  attributeVector.release();
}



/* ****************************************************************************
*
* Entity::hideIdAndType
*
* Changes the attribute controlling if id and type are rendered in the JSON
*/
void Entity::hideIdAndType(bool hide)
{
  renderId = !hide;
}
