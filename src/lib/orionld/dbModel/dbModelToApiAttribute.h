#ifndef SRC_LIB_ORIONLD_DBMODEL_DBMODELTOAPIATTRIBUTE_H_
#define SRC_LIB_ORIONLD_DBMODEL_DBMODELTOAPIATTRIBUTE_H_

/*
*
* Copyright 2022 FIWARE Foundation e.V.
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
extern "C"
{
#include "kjson/KjNode.h"                                        // KjNode
}

#include "common/RenderFormat.h"                                 // RenderFormat
#include "orionld/types/OrionldProblemDetails.h"                 // OrionldProblemDetails



// -----------------------------------------------------------------------------
//
// dbModelToApiAttribute - produce an NGSI-LD API Attribute from its DB format
//
extern void dbModelToApiAttribute(KjNode* attrP, bool sysAttrs, bool eqsForDots);



// -----------------------------------------------------------------------------
//
// dbModelToApiAttribute2 -
//
extern KjNode* dbModelToApiAttribute2(KjNode* dbAttrP, KjNode* datasetP, bool sysAttrs, RenderFormat renderFormat, char* lang, OrionldProblemDetails* pdP);

#endif  // SRC_LIB_ORIONLD_DBMODEL_DBMODELTOAPIATTRIBUTE_H_
