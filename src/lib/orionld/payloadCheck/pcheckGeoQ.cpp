/*
*
* Copyright 2019 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
#include <string.h>                                             // strcmp
extern "C"
{
#include "kjson/KjNode.h"                                       // KjNode
#include "kjson/kjParse.h"                                      // kjParse
}

#include "logMsg/logMsg.h"                                      // LM_*
#include "logMsg/traceLevels.h"                                 // Lmt*

#include "orionld/common/orionldState.h"                        // orionldState
#include "orionld/common/orionldError.h"                        // orionldError
#include "orionld/common/CHECK.h"                               // STRING_CHECK, ...
#include "orionld/common/dotForEq.h"                            // dotForEq
#include "orionld/types/OrionldProblemDetails.h"                // OrionldProblemDetails
#include "orionld/types/OrionldGeometry.h"                      // OrionldGeometry
#include "orionld/types/OrionldGeoInfo.h"                       // OrionldGeoInfo
#include "orionld/context/orionldAttributeExpand.h"             // orionldAttributeExpand
#include "orionld/payloadCheck/PCHECK.h"                        // PCHECK_*
#include "orionld/payloadCheck/pCheckGeometry.h"                // pCheckGeometry
#include "orionld/payloadCheck/pCheckGeoCoordinates.h"          // pCheckGeoCoordinates
#include "orionld/payloadCheck/pCheckGeorel.h"                  // pCheckGeorel
#include "orionld/payloadCheck/pcheckGeoQ.h"                    // Own interface



// ----------------------------------------------------------------------------
//
// pcheckGeoQ -
//
OrionldGeoInfo* pcheckGeoQ(KjNode* geoqNodeP, bool isSubscription)
{
  KjNode*          geometryP    = NULL;
  KjNode*          coordinatesP = NULL;
  KjNode*          georelP      = NULL;
  KjNode*          geopropertyP = NULL;
  OrionldGeoInfo*  geoInfoP     = (OrionldGeoInfo*) kaAlloc(&orionldState.kalloc, sizeof(OrionldGeoInfo));

  if (geoInfoP == NULL)
  {
    orionldError(OrionldInternalError, "Out of memory", "allocating OrionldGeoInfo", 500);
    return NULL;
  }

  for (KjNode* itemP = geoqNodeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (strcmp(itemP->name, "geometry") == 0)
    {
      DUPLICATE_CHECK_R(geometryP, "geometry", itemP, NULL);
      STRING_CHECK_R(geometryP, "the 'geometry' field of a GeoJSON object must be a JSON String", NULL);
    }
    else if (strcmp(itemP->name, "coordinates") == 0)
    {
      DUPLICATE_CHECK_R(coordinatesP, "coordinates", itemP, NULL);
      PCHECK_STRING_OR_ARRAY_R(coordinatesP, 0, "Invalid Data Type", "the 'coordinates' field of a GeoJSON object must be a JSON Array (or String)", 400, NULL);
    }
    else if (strcmp(itemP->name, "georel") == 0)
    {
      DUPLICATE_CHECK_R(georelP, "georel", itemP, NULL);
      STRING_CHECK_R(georelP, "the 'georel' field of a GeoJSON object must be a JSON String", NULL);
    }
    else if (strcmp(itemP->name, "geoproperty") == 0)
    {
      DUPLICATE_CHECK_R(geopropertyP, "geoproperty", itemP, NULL);
      STRING_CHECK_R(geopropertyP, "the 'geoproperty' field of a GeoJSON object must be a JSON String", NULL);
    }
    else
    {
      orionldError(OrionldBadRequestData, "Invalid Payload Data - invalid field in geoQ", itemP->name, 400);
      return NULL;
    }
  }

  if (geometryP == NULL)
  {
    orionldError(OrionldBadRequestData, "Invalid Payload Data", "mandatory geoQ field 'geometry' is missing", 400);
    return NULL;
  }

  if (coordinatesP == NULL)
  {
    orionldError(OrionldBadRequestData, "Invalid Payload Data", "mandatory geoQ field 'coordinates' is missing", 400);
    return NULL;
  }

  if (georelP == NULL)
  {
    orionldError(OrionldBadRequestData, "Invalid Payload Data", "mandatory geoQ field 'georel' is missing", 400);
    return NULL;
  }

  if (pCheckGeometry(geometryP->value.s, &geoInfoP->geometry, isSubscription) == false)
  {
    // orionldError(OrionldBadRequestData, "Invalid Payload Data", detail, 400);
    return NULL;
  }

  if (coordinatesP->type == KjString)
  {
    geoInfoP->coordinates = kjParse(orionldState.kjsonP, coordinatesP->value.s);
    if (geoInfoP->coordinates == NULL)
    {
      orionldError(OrionldBadRequestData, "Invalid GeoJSON", "'coordinates' as string is not a valid JSON Array", 400);
      return NULL;
    }
  }
  else
    geoInfoP->coordinates = coordinatesP;

  if (pCheckGeoCoordinates(geoInfoP->coordinates, geoInfoP->geometry) == false)
  {
    //
    // Not overwriting - putting the call to orionldError back once I have error stacking
    // orionldError(OrionldBadRequestData, "Invalid Payload Data", "invalid coordinates", 400);
    //
    return NULL;
  }

  if (pCheckGeorel(georelP, geoInfoP) == false)
    return NULL;  // pCheckGeorel calls orionldError

  //
  // If geoproperty has been given, the name must be expanded
  //
  if (geopropertyP != NULL)
  {
    char* pName = geopropertyP->value.s;

    if (strcmp(pName, "location") != 0)
    {
      geopropertyP->value.s = orionldAttributeExpand(orionldState.contextP, pName, true, NULL);

      //
      // FIXME: No need, as the attr name is in the "value", not the "key".
      //        However, I  DO  need this for geo queries as the attr name in "attrs" in BD contains = and not .
      //        I could fix this, to only do dotForEq for queries.
      //        For now, let's just leave it as is.
      //        Safer, also for DB compatibilities with older versions of Orion-LD
      //
      dotForEq(geopropertyP->value.s);
    }

    geoInfoP->geoProperty = geopropertyP->value.s;
  }

  return geoInfoP;
}
