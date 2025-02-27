/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
#include <string.h>                                            // strcpy

#include "orionld/payloadCheck/pCheckUri.h"                    // pCheckUri
#include "orionld/common/linkCheck.h"                          // Own interface



// -----------------------------------------------------------------------------
//
// linkCheck -
//
// Example link:
//   <https://fiware.github.io/X/Y/Z.jsonld>; rel="http://www.w3.org/ns/json-ld#context"; type="application/ld+json"
//
// NOTE
//   The initial '<' is stepped over before clling this function, by linkHeaderCheck() in orionldMhdConnectionTreat.cpp
//
bool linkCheck(char* link, char** detailsP)
{
  char* cP = link;

  while (*cP != '>')
  {
    if (*cP == 0)
    {
      *detailsP = (char*) "missing '>' at end of URL of link";
      return false;
    }

    ++cP;
  }

  *cP = 0;  // End of string for the URL

  if (pCheckUri(link, "Link", true) == false)
  {
    *detailsP = (char*) "Not a valid URI";
    return false;
  }

  //
  // FIXME: Parse the 'rel' and 'type' as well ?
  //

  return true;
}
