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
#include "kbase/kMacros.h"                                       // K_VEC_SIZE
#include "kjson/KjNode.h"                                        // KjNode
#include "kjson/kjLookup.h"                                      // kjLookup
#include "kjson/kjBuilder.h"                                     // kjChildRemove
#include "kjson/kjClone.h"                                       // kjClone
}

#include "logMsg/logMsg.h"                                       // LM_*

#include "common/globals.h"                                      // parse8601Time
#include "orionld/mongoc/mongocEntityUpdate.h"                   // mongocEntityUpdate
#include "orionld/mongoc/mongocEntityLookup.h"                   // mongocEntityLookup
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldError.h"                         // orionldError
#include "orionld/common/dotForEq.h"                             // dotForEq
#include "orionld/common/eqForDot.h"                             // eqForDot
#include "orionld/common/orionldPatchApply.h"                    // orionldPatchApply
#include "orionld/types/OrionldHeader.h"                         // orionldHeaderAdd
#include "orionld/types/OrionldAlteration.h"                     // OrionldAlteration, orionldAlterationType
#include "orionld/kjTree/kjTimestampAdd.h"                       // kjTimestampAdd
#include "orionld/kjTree/kjArrayAdd.h"                           // kjArrayAdd
#include "orionld/kjTree/kjStringValueLookupInArray.h"           // kjStringValueLookupInArray
#include "orionld/payloadCheck/pCheckEntity.h"                   // pCheckEntity
#include "orionld/dbModel/dbModelFromApiEntity.h"                // dbModelFromApiEntity
#include "orionld/dbModel/dbModelToApiAttribute.h"               // dbModelToApiAttribute
#include "orionld/notifications/orionldAlterations.h"            // orionldAlterations
#include "orionld/serviceRoutines/orionldPatchEntity2.h"         // Own Interface



//
// To PATCH an Entity:
// 1. GET the entity from DB - in DB-Model style
// 2. Convert the request payload to a DB-Model style Entity (includes modifiedAt but not createdAt) - dbModelFromApiXXX()
// 3. Compare the two trees and come up with an array of changes (orionldEntityPatchTree - output from orionldPatchXXX functions):
//    [
//      { PATH, TREE, op },
//      { PATH, TREE, op },
//      ...
//    ]
//
//    E.g. (P1: modification of a compound value of a sub-attr   +  P2: addition of an attribute + P3: removal of a sub-attr):
//    [
//      {
//        "PATH": "attrs.P1.md.Sub-R.value.F",
//        "TREE": <KjNode tree of the new value>,
//        "op": "Modify" - Not needed - that is the default
//      },
//      {
//        "PATH": "attrs.P2",
//        "TREE": <KjNode tree of the attribute - DB Model style>,
//        "op": "Add" - Not needed - "modify" and "add" is the same for mongo - REPLACE/APPEND
//      },
//      {
//        "PATH": "attrs.P3.md.SP1",
//        "TREE": null,
//        "op": "delete"  - Not needed - "TREE" is null, so, the Delete info is already present
//      }
//    ]
//
//   The PATCH-function (not DB dependent) pinpoints every node in the tree where there is s difference
//   The PATH is the pointer to the differing node.
//   The TREE is the new value for the differing node.
//
//   TREE.type == KjNull means removal
//



// -----------------------------------------------------------------------------
//
// patchTreeItemAdd -
//
static void patchTreeItemAdd(KjNode* patchTree, const char* path, KjNode* valueP, const char* op)
{
  KjNode* arrayItemP = kjObject(orionldState.kjsonP, NULL);
  KjNode* pathP      = kjString(orionldState.kjsonP, "PATH", path);

  // valueP is STOLEN here - careful with its previous linked list ...
  valueP->name = (char*) "TREE";
  kjChildAdd(arrayItemP, pathP);
  kjChildAdd(arrayItemP, valueP);

  if (op != NULL)
  {
    KjNode* opP = kjString(orionldState.kjsonP, "op", op);
    kjChildAdd(arrayItemP, opP);
  }

  kjChildAdd(patchTree, arrayItemP);
}



// -----------------------------------------------------------------------------
//
// namesArrayMergeToPatchTree - only used if we both add and delete attrs in the same operation
//
static void namesArrayMergeToPatchTree(KjNode* patchTree, const char* path, KjNode* namesP, KjNode* addedP, KjNode* removedP)
{
  for (KjNode* rmP = removedP->value.firstChildP; rmP != NULL; rmP = rmP->next)
  {
    KjNode* itemP = kjStringValueLookupInArray(namesP, rmP->value.s);

    if (itemP)
      kjChildRemove(namesP, itemP);
  }

  // Now add the strings in addedP:
  if (namesP->value.firstChildP == NULL)  // Array is empty
    namesP->value.firstChildP = addedP->value.firstChildP;
  else
    namesP->lastChild->next = addedP->value.firstChildP;

  namesP->lastChild = addedP->lastChild;

  patchTreeItemAdd(patchTree, path, namesP, NULL);
}



// -----------------------------------------------------------------------------
//
// namesToPatchTree -
//
static void namesToPatchTree(KjNode* patchTree, const char* path, KjNode* namesP, KjNode* addedP, KjNode* removedP)
{
  // Fix the attrNames/mdNames $set/$pull/$pop
  char* namesPath;

  if (path == NULL)
    namesPath = (char*) "attrNames";
  else
  {
    int namesPathLen = ((path != NULL)? strlen(path) + 1 : 0) + 8;  // 8: strlen("mdNames") + 1

    namesPath = kaAlloc(&orionldState.kalloc, namesPathLen);
    snprintf(namesPath, namesPathLen, "%s.mdNames", path);
  }

  bool added   = false;
  bool removed = false;

  if ((addedP   != NULL) && (addedP->value.firstChildP   != NULL))  added   = true;
  if ((removedP != NULL) && (removedP->value.firstChildP != NULL))  removed = true;

  if      ((added == true)   && (removed == false))   patchTreeItemAdd(patchTree, namesPath, addedP,   "PUSH");
  else if ((added == false)  && (removed == true))    patchTreeItemAdd(patchTree, namesPath, removedP, "PULL");
  else if ((added == true)   && (removed == true))    namesArrayMergeToPatchTree(patchTree, namesPath, namesP, addedP, removedP);
}



// -----------------------------------------------------------------------------
//
// orionldEntityPatchTree -
//
//   - if item exists in old but not in new it is kept (nothing added to 'patchTree')
//   - if item exists in new but not in old it is added
//   - it item exists in both old and new:
//     - if new's value is null             - REMOVE item
//     - If JSON type of new item is Simple - new REPLACES old item
//     - if items JSON type differ          - new REPLACES old item
//     - if Array                           - new REPLACES old item
//     - If Object (both of them)           - recursive call
//
// Might be a good idea to sort both trees before starting to process ...
//
static void orionldEntityPatchTree(KjNode* oldP, KjNode* newP, char* path, KjNode* patchTree)
{
  if (newP == NULL)  // Not sure this is ever a possibility ...
    return;

  //
  // In JSON-LD, null is expressed in a different way ...
  //
  if ((newP->type == KjString) && (strcmp(newP->value.s, "urn:ngsi-ld:null") == 0))
    newP->type = KjNull;

  if (oldP == NULL)  // It's NEW - doesn't exist in OLD - simply ADD
  {
    // Except if it's a NULL. If NULL, then it is ignored
    if (newP->type != KjNull)
      patchTreeItemAdd(patchTree, path, newP, NULL);

    return;
  }


  //
  // NULL as RHS means REMOVAL
  //
  if (newP->type == KjNull)
  {
    patchTreeItemAdd(patchTree, path, newP, "DELETE");
    return;
  }

  if (newP->type != oldP->type)  // Different type?  - overwrite the old
  {
    patchTreeItemAdd(patchTree, path, newP, NULL);
    return;
  }

  bool change       = false;
  bool nonCompound  = false;  // We're done if it's a SIMPLE TYPE (String, Number, Bool)

  if      (newP->type == KjString)   { if (strcmp(newP->value.s, oldP->value.s) != 0)  change = true; nonCompound = true; }
  else if (newP->type == KjInt)      { if (newP->value.i != oldP->value.i)             change = true; nonCompound = true; }
  else if (newP->type == KjFloat)    { if (newP->value.f != oldP->value.f)             change = true; nonCompound = true; }
  else if (newP->type == KjBoolean)  { if (newP->value.b != oldP->value.b)             change = true; nonCompound = true; }

  if (change == true)
    patchTreeItemAdd(patchTree, path, newP, NULL);

  if (nonCompound == true)
    return;

  if (newP->type == KjArray)  // If it's an Array, we replace the old array (by definition)
  {
    patchTreeItemAdd(patchTree, path, newP, NULL);
    return;
  }

  // Both newP and oldP are JSON OBJECT - entering inside the objects to find differences

  KjNode* namesP     = kjLookup(newP, ".names");
  KjNode* addedP     = kjLookup(newP, ".added");
  KjNode* removedP   = kjLookup(newP, ".removed");



  //
  // CAREFUL with the linked lists of newP - the recursive calls may invoke patchTreeItemAdd and that *REMOVES ITEMS FROM A LIST*
  // => Can't use the normal "for" - must be a "while and a next pointer"
  //
  KjNode* newItemP   = newP->value.firstChildP;
  KjNode* next;

  while (newItemP != NULL)
  {
    // Skip "hidden" fields
    if (newItemP->name[0] == '.')
    {
      newItemP = newItemP->next;
      continue;
    }

    next = newItemP->next;

    KjNode* oldItemP = kjLookup(oldP, newItemP->name);
    char*   newPath;

    if (path == NULL)
      newPath = newItemP->name;
    else
    {
      int     newPathLen  = strlen(path) + 1 + strlen(newItemP->name) + 1;
      char*   newPathV    = kaAlloc(&orionldState.kalloc, newPathLen);

      snprintf(newPathV, newPathLen, "%s.%s", path, newItemP->name);
      newPath = newPathV;
    }

    orionldEntityPatchTree(oldItemP, newItemP, newPath, patchTree);  // Recursive call

    newItemP = next;
  }

  if (namesP != NULL)
    namesToPatchTree(patchTree, path, namesP, addedP, removedP);
}



// -----------------------------------------------------------------------------
//
// dbEntityFields -
//
static bool dbEntityFields(KjNode* dbEntityP, const char* entityId, char** entityTypeP, KjNode** attrsPP)
{
  KjNode* _idP = kjLookup(dbEntityP, "_id");

  if (_idP == NULL)
  {
    orionldError(OrionldInternalError, "Database Error (entity without _id)", entityId, 500);
    return false;
  }

  KjNode* dbTypeP = kjLookup(_idP, "type");
  if (dbTypeP == NULL)
  {
    orionldError(OrionldInternalError, "Database Error (entity without type)", entityId, 500);
    return false;
  }

  if (dbTypeP->type != KjString)
  {
    orionldError(OrionldInternalError, "Database Error (entity type not a String)", kjValueType(dbTypeP->type), 500);
    return false;
  }
  *entityTypeP = dbTypeP->value.s;

  *attrsPP  = kjLookup(dbEntityP, "attrs");
  if (*attrsPP == NULL)
  {
    orionldError(OrionldInternalError, "Database Error (entity without attrs field)", entityId, 500);
    return false;
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// orionldAlterationsPresent -
//
void orionldAlterationsPresent(OrionldAlteration* altP)
{
  LM_K(("Entity Altered:  Entity Id:   %s", altP->entityId));
  LM_K(("Entity Altered:  Entity Type: %s", altP->entityType));
  LM_K(("Entity Altered:  Attributes:  %d", altP->alteredAttributes));

  for (int ix = 0; ix < altP->alteredAttributes; ix++)
  {
    LM_K(("Entity Altered:    Attribute:  %s", altP->alteredAttributeV[ix].attrName));
    LM_K(("Entity Altered:    Attribute:  %s", altP->alteredAttributeV[ix].attrNameEq));
    LM_K(("Entity Altered:    Alteration: %s", orionldAlterationType(altP->alteredAttributeV[ix].alterationType)));
  }
}



// ----------------------------------------------------------------------------
//
// orionldPatchEntity2 -
//
bool orionldPatchEntity2(void)
{
  if (experimental == false)
  {
    orionldError(OrionldResourceNotFound, "Service Not Found", orionldState.urlPath, 404);
    return false;
  }

  char*    entityId    = orionldState.wildcard[0];
  KjNode*  dbEntityP;

  //
  // FIXME: would really like to only extract the attrs in the patch ...
  //        But, for that I'd need to:
  //        * check the attr name for forbidden chars
  //        * expand it
  //        * do dotForEq
  //        * create a KjNode array with all toplevel field names
  //        * sort out non-attribute names (id, type, scope, ...)
  //
  //        All that is done by pCheckEntity, but pCheckEntity needs the DB Entity :(
  //        The chicken and the egg ...
  //
  //        Not really worth it.
  //        At least, postponed (not forgotten) for now.
  //
  dbEntityP = mongocEntityLookup(entityId);

  if (dbEntityP == NULL)
  {
    orionldError(OrionldResourceNotFound, "Entity does not exist", entityId, 404);
    return false;
  }

  char*   entityType  = NULL;
  KjNode* dbAttrsP    = NULL;

  if (dbEntityFields(dbEntityP, entityId, &entityType, &dbAttrsP) == false)
    return false;

  //
  // FIXME: change pCheckEntity param no 3 to be not only the attributes, but the entire entity (dbEntityP)
  //
  if (pCheckEntity(orionldState.requestTree, false, dbAttrsP) == false)
  {
    LM_W(("Invalid payload body. %s: %s", orionldState.pd.title, orionldState.pd.detail));
    return false;
  }

  orionldState.alterations = orionldAlterations(entityId, entityType, orionldState.requestTree, dbAttrsP, false);
  orionldAlterationsPresent(orionldState.alterations);
  orionldState.alterations->dbEntityP = kjClone(orionldState.kjsonP, dbEntityP);

  //
  // For TRoE we need a tree with all those attributes that have been patched (part of incoming tree)
  // but, with their current value in the database PATCHED with their new values
  //
  if (troe)
  {
    // 1. Get from DB (dbAttrsP) all attributes that have been touched by the PATCH
    // 2. Convert the attributes to API Model
    // 3. Perform the PATCH on the attributes
    KjNode* troeTree = kjObject(orionldState.kjsonP, NULL);

    for (KjNode* patchedAttrP = orionldState.requestTree->value.firstChildP; patchedAttrP != NULL; patchedAttrP = patchedAttrP->next)
    {
      KjNode* dbAttrP;
      dotForEq(patchedAttrP->name);

      if ((dbAttrP = kjLookup(dbAttrsP, patchedAttrP->name)) != NULL)
      {
        KjNode* troeAttrP = kjClone(orionldState.kjsonP, dbAttrP);

        dbModelToApiAttribute(troeAttrP, orionldState.uriParamOptions.sysAttrs, false);
        kjChildAdd(troeTree, troeAttrP);
      }
      eqForDot(patchedAttrP->name);
    }

    orionldState.patchBase = troeTree;
  }

  //
  // FIXME: If I destroy the tree, how do I handle notifications/forwarding later ... ?
  //        I extract the part that is to be forwarded from the tree before I destroy
  //        If non-exclusive, might be I both forward and do it locally - kjClone(attrP)
  //
  if (dbModelFromApiEntity(orionldState.requestTree, dbEntityP, false, NULL, NULL) == false)
  {
    LM_W(("dbModelFromApiEntity: %s: %s", orionldState.pd.title, orionldState.pd.detail));
    return false;
  }


  //
  // Due to the questionable database model of Orion (that Orion-LD has inherited - backwards compatibility),
  // the attributes that have been added/removed must be added/removed to a redundant database field called "attrNames".
  //
  // It's not possible to both remove and add items from a field in a single operation in mongo, so a list of all
  // added attributes and another list of all deleted attributes must be maintained for the purpose of updating "attrNames".
  //
  // The very same is true for the sub-attributes of an attribute - the database field "mdNames" is an array of all sub-attributes.
  //
  // The lists of added/deleted attributes are stored in arrays in the tree: ".added" + ".removed"
  // The lists of added/deleted sub-attributes, same same - ".added" + ".removed", under the attribute
  //
  // orionldEntityPatch returns a KjNode array of objects describing the modifications for the patch.
  // For details, see "To PATCH an Entity" above.
  //
  KjNode* patchTree     = kjArray(orionldState.kjsonP, NULL);
  KjNode* dbAttrsObject = kjObject(orionldState.kjsonP, NULL);

  kjChildAdd(dbAttrsObject, dbAttrsP);
  orionldState.requestTree->name = NULL;
  orionldEntityPatchTree(dbAttrsObject, orionldState.requestTree, NULL, patchTree);

  orionldState.alterations->inEntityP       = patchTree;  // Not sure this is needed - alteredAttributeV should be used instead ... Right?
  orionldState.alterations->finalApiEntityP = NULL;

  bool b = mongocEntityUpdate(entityId, patchTree);  // Added/Removed (sub-)attrs are found in arrays named ".added" and ".removed"
  if (b == false)
  {
    bson_error_t* errP = &orionldState.mongoc.error;  // Can't be in orionldState - DB Dependant!!!

    LM_E(("mongocEntityUpdate(%s): [%d.%d]: %s", entityId, errP->domain, errP->code, errP->message));

    if (errP->code == 12)  orionldError(OrionldResourceNotFound, "Entity not found", entityId, 404);
    else                   orionldError(OrionldInternalError, "Internal Error", errP->message, 500);

    return false;
  }

  orionldState.httpStatusCode = 204;

  if (troe)
    orionldState.requestTree = patchTree;

  return true;
}
