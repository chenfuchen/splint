/*
** LCLint - annotation-assisted static program checker
** Copyright (C) 1994-2000 University of Virginia,
**         Massachusetts Institute of Technology
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 2 of the License, or (at your
** option) any later version.
** 
** This program is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** The GNU General Public License is available from http://www.gnu.org/ or
** the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
** MA 02111-1307, USA.
**
** For information on lclint: lclint-request@cs.virginia.edu
** To report a bug: lclint-bug@cs.virginia.edu
** For more information: http://lclint.cs.virginia.edu
*/
/*
** clabstract.c
**
** ASTs for C grammar
**
*/

# include "lclintMacros.nf"
# include "llbasic.h"
# include "cgrammar.h"

# ifndef NOLCL
# include "usymtab_interface.h"
# endif

# include "structNames.h"
# include "nameChecks.h"

# ifdef SANITIZER
# include "sgrammar_tokens.h"
# else
# include "cgrammar_tokens.h"
# endif

/*
** Lots of variables are needed because of interactions with the
** parser.  This is easier than restructuring the grammar so the
** right values are available in the right place.
*/

static /*@only@*/ sRefSet fcnModifies = sRefSet_undefined;
static /*@only@*/ /*@null@*/ specialClauses specClauses = specialClauses_undefined;
static bool fcnNoGlobals = FALSE;
static bool ProcessingVars = FALSE;
static bool ProcessingParams = FALSE;
static bool ProcessingGlobals = FALSE;
static bool ProcessingTypedef = FALSE;
static bool ProcessingIterVars = FALSE;
static /*@only@*/ qtype processingType = qtype_undefined;
static uentry currentIter = uentry_undefined;
static globSet currentGlobals = globSet_undefined;
static /*@dependent@*/ uentryList saveParamList;  /* for old style functions */
static /*@owned@*/ uentry saveFunction = uentry_undefined;
static int saveIterParamNo;
static idDecl fixStructDecl (/*@returned@*/ idDecl p_d);
static void checkTypeDecl (uentry p_e, ctype p_rep);
static /*@dependent@*/ fileloc saveStoreLoc = fileloc_undefined;
static storageClassCode storageClass = SCNONE;
static void declareEnumList (/*@temp@*/ enumNameList p_el, ctype p_c, fileloc p_loc);
static void resetGlobals (void);
static qual specialFunctionCode = QU_UNKNOWN;
static bool argsUsed = FALSE;

static bool hasSpecialCode (void)
{
  return (specialFunctionCode != QU_UNKNOWN);
}

extern void setArgsUsed (void)
{
  if (argsUsed)
    {
      voptgenerror
	(FLG_SYNTAX,
	 cstring_makeLiteral ("Multiple ARGSUSED comments for one function"),
	 g_currentloc);
    }

  argsUsed = TRUE;
}

static void reflectArgsUsed (uentry ue)
{
  if (argsUsed)
    {
      if (uentry_isFunction (ue))
	{
	  uentryList params = uentry_getParams (ue);

	  uentryList_elements (params, el)
	    {
	      uentry_setUsed (el, fileloc_undefined);
	    } end_uentryList_elements ;
	}

      argsUsed = FALSE;
    }
}
	      
extern void setSpecialFunction (qual qu)
{
  if (specialFunctionCode != QU_UNKNOWN)
    {
      voptgenerror (FLG_SYNTAX,
		    message ("Multiple special function codes: %s, %s "
			     "(first code is ignored)",
			     qual_unparse (specialFunctionCode),
			     qual_unparse (qu)),
		    g_currentloc);
    }

  specialFunctionCode = qu;
}

static void reflectSpecialCode (uentry ue)
{
  switch (specialFunctionCode)
    {
    case QU_UNKNOWN: break;
    case QU_PRINTFLIKE:
      uentry_setPrintfLike (ue);
      break;
    case QU_SCANFLIKE:
      uentry_setScanfLike (ue);
      break;
    case QU_MESSAGELIKE:
      uentry_setMessageLike (ue);
      break;
    BADDEFAULT;
    }

  specialFunctionCode = QU_UNKNOWN;
}

static void resetStorageClass (void)
{
  qtype_free (processingType);
  processingType = qtype_undefined;
  storageClass = SCNONE;
}

static void reflectModGlobs (uentry ue)
{
  if (fcnNoGlobals)
    {
      llassert (globSet_isUndefined (currentGlobals));

      uentry_setGlobals (ue, globSet_undefined);
      fcnNoGlobals = FALSE;
    }
  else if (globSet_isDefined (currentGlobals))
    {
      uentry_setGlobals (ue, currentGlobals);
      currentGlobals = globSet_undefined;
    }
  else
    {
      ; /* no globals */
    }

  if (sRefSet_isDefined (fcnModifies))
    {
      uentry_setModifies (ue, fcnModifies);
      fcnModifies = sRefSet_undefined;
    }

  if (uentry_isFunction (ue))
    {
      uentry_setSpecialClauses (ue, specClauses);
      specClauses = NULL;
      DPRINTF (("Done with spec clauses"));
    }
}

static void reflectStorageClass (uentry u)
{
  if (storageClass == SCSTATIC)
    {
      uentry_setStatic (u);
    }
  else if (storageClass == SCEXTERN)
    {
      uentry_setExtern (u);
    }
  else
    {
      ; /* no storage class */
    }

  }

void storeLoc ()
{
  saveStoreLoc = g_currentloc;
}

void setFunctionNoGlobals (void)
{
  llassert (globSet_isUndefined (currentGlobals));
  fcnNoGlobals = TRUE;
}

void
  setFunctionStateSpecialClause (lltok stok, specialClauseKind kind, 
				 sRefSet s, 
				 /*@unused@*/ lltok etok)
{
  int tok = lltok_getTok (stok);

  switch (tok)
    {
    case QPRECLAUSE:
      specClauses = specialClauses_add (specClauses, 
					specialClause_create (TK_BEFORE, kind, s));
      break;
    case QPOSTCLAUSE:
      specClauses = specialClauses_add (specClauses, 
					specialClause_create (TK_AFTER, kind, s));
      break;
    default:
      sRefSet_free (s);
      BADBRANCH;
    }

  DPRINTF (("Added to specclauses: %s", specialClauses_unparse (specClauses)));
}

void setFunctionSpecialClause (lltok stok, sRefSet s, 
			       /*@unused@*/ lltok etok)
{
  int tok = lltok_getTok (stok);

  switch (tok)
    {
    case QUSES:
      specClauses = specialClauses_add (specClauses, specialClause_createUses (s));
      break;
    case QDEFINES:
      specClauses = specialClauses_add (specClauses, specialClause_createDefines (s));
      break;
    case QALLOCATES:
      specClauses = specialClauses_add (specClauses, specialClause_createAllocates (s));
      break;
    case QSETS:
      specClauses = specialClauses_add (specClauses, specialClause_createSets (s));
      break;
    case QRELEASES:
      specClauses = specialClauses_add (specClauses, specialClause_createReleases (s));
      break;
    default:
      sRefSet_free (s);
      BADBRANCH;
    }

  DPRINTF (("Added to specclauses: %s", specialClauses_unparse (specClauses)));
}

void setFunctionModifies (sRefSet s)
{
  sRefSet_free (fcnModifies);
  fcnModifies = s;
}

static void reflectGlobalQualifiers (sRef sr, qualList quals)
{
  qualList_elements (quals, qel)
    {
      if (qual_isGlobalQual (qel)) /* undef, killed */
	{
	  sstate oldstate = sRef_getDefState (sr);
	  sstate defstate = sstate_fromQual (qel);
	  
	  if ((oldstate == SS_UNDEFGLOB && defstate == SS_KILLED)
	      || (oldstate == SS_KILLED && defstate == SS_UNDEFGLOB))
	    {
	      defstate = SS_UNDEFKILLED;
	    }
	  else 
	    {
	      ; /* any errors? */
	    }
	  
	  sRef_setDefState (sr, defstate, fileloc_undefined);
	}
      else if (qual_isAllocQual (qel)) /* out, partial, reldef, etc. */
	{
	  ctype realType = sRef_getType (sr);
	  sstate defstate = sstate_fromQual (qel);
	  
	  if (qual_isRelDef (qel))
	    {
	      ; /* okay anywhere */
	    }
	  else
	    {
	      if (!ctype_isAP (realType) 
		  && !ctype_isSU (realType)
		  && !ctype_isUnknown (realType)
		  && !ctype_isAbstract (sRef_getType (sr)))
		{
		  llerror 
		    (FLG_SYNTAX, 
		     message ("Qualifier %s used on non-pointer or struct: %q",
			      qual_unparse (qel), sRef_unparse (sr)));
		  
		}
	    }
	  
	  sRef_setDefState (sr, defstate, fileloc_undefined);
	}
      else if (qual_isNull (qel))
	{
	  sRef_setNullState (sr, NS_POSNULL, fileloc_undefined);
	}
      else if (qual_isRelNull (qel))
	{
	  sRef_setNullState (sr, NS_RELNULL, fileloc_undefined);
	}
      else if (qual_isNotNull (qel))
	{
	  sRef_setNullState (sr, NS_MNOTNULL, fileloc_undefined);
	}
      else
	{
	  if (qual_isCQual (qel))
	    {
	      ; /* okay */
	    }
	  else
	    {
	      llerror (FLG_SYNTAX,
		       message ("Qualifier %s cannot be used in a globals list",
				qual_unparse (qel)));
	    }
	}
    } end_qualList_elements;
}

void globListAdd (sRef sr, qualList quals)
{
  if (sRef_isValid (sr))
    {
      sRef sc = sRef_copy (sr);

      reflectGlobalQualifiers (sc, quals);
      currentGlobals = globSet_insert (currentGlobals, sc);
    }
}

extern void declareCIter (cstring name, /*@owned@*/ uentryList params)
{
  uentry ue;

  ue = uentry_makeIter (name, 
			ctype_makeFunction (ctype_void, params), 
			fileloc_copy (g_currentloc));

  usymtab_supEntry (uentry_makeEndIter (name, fileloc_copy (g_currentloc)));

  reflectModGlobs (ue);

  ue = usymtab_supGlobalEntryReturn (ue);
}

extern void nextIterParam (void)
{
  llassert (ProcessingIterVars);
  saveIterParamNo++;
}

extern int iterParamNo (void)
{
  llassert (ProcessingIterVars);
  return saveIterParamNo;
}

/*
** yucky hacks to put it in the right place
*/

/*@only@*/ uentry 
makeCurrentParam (idDecl t)
{
  uentry ue;

  saveStoreLoc = fileloc_undefined;

  /* param number unknown */

  ue = uentry_makeParam (t, 0);
  return ue;
}

ctype
declareUnnamedEnum (enumNameList el)
{
  ctype ret = usymtab_enumEnumNameListType (el);
  ctype rt;
  uentry e;

  if (ctype_isDefined (ret))
    {
      rt = ret;
      e = uentry_makeEnumTagLoc (ctype_enumTag (rt), ret);

      reflectStorageClass (e);
      usymtab_supGlobalEntry (e);
      
      declareEnumList (el, ret, g_currentloc);    
      enumNameList_free (el);
    }
  else
    {
      ctype ct = ctype_createEnum (fakeTag (), el);

      e = uentry_makeEnumTagLoc (ctype_enumTag (ctype_realType (ct)), ct);
      reflectStorageClass (e);

      e = usymtab_supGlobalEntryReturn (e);
      rt = uentry_getAbstractType (e);
      declareEnumList (el, ct, g_currentloc);    
    }
  
  return (rt);
}

ctype
declareEnum (cstring ename, enumNameList el)
{
  ctype cet;
  uentry e;

  llassert (cstring_isDefined (ename));

  cet = ctype_createEnum (ename, el);
  e = uentry_makeEnumTagLoc (ename, cet);
  reflectStorageClass (e);
  e = usymtab_supGlobalEntryReturn (e);
  cet = uentry_getType (e);
  declareEnumList (el, cet, uentry_whereLast (e));    
  return (uentry_getAbstractType (e));
}

static void
declareEnumList (enumNameList el, ctype c, fileloc loc)
{
  bool boolnames = FALSE;
  bool othernames = FALSE;

  (void) context_getSaveLocation (); /* undefine it */

  if (context_maybeSet (FLG_NUMENUMMEMBERS))
    {
      int maxnum = context_getValue (FLG_NUMENUMMEMBERS);
      int num = enumNameList_size (el);

      if (num > maxnum)
	{
	  voptgenerror 
	    (FLG_NUMENUMMEMBERS,
	     message ("Enumerator %s declared with %d members (limit is set to %d)",
		      ctype_unparse (c), num, maxnum),
	     loc);
	}
    }

  enumNameList_elements (el, e)
    {
      uentry ue = usymtab_lookupExposeGlob (e);
      ctype ct = uentry_getType (ue);

      llassert (uentry_isEnumConstant (ue));

      if (ctype_isUnknown (ct))
	{
	  uentry_setType (ue, c);
	}
      else
	{
	  if (cstring_equal (e, context_getFalseName ())
	      || cstring_equal (e, context_getTrueName ()))
	    {
	      if (othernames) 
		{
		  if (optgenerror 
		      (FLG_INCONDEFS,
		       message ("Enumerator mixes boolean name (%s) with "
				"non-boolean names",
				e),
		       uentry_whereLast (ue)))
		    {
		      ;
		    }
		}
	      
	      boolnames = TRUE;
	      uentry_setType (ue, ctype_bool);
	    }
	  else 
	    {
	      if (boolnames) 
		{
		  if (optgenerror 
		      (FLG_INCONDEFS,
		       message ("Enumerator mixes boolean names (%s, %s) with "
				"non-boolean name: %s",
				context_getTrueName (),
				context_getFalseName (),
				e),
		       uentry_whereLast (ue)))
		    {
		      ;
		    }
		}

	      othernames = TRUE;
	    }

	  if (!ctype_match (c, ct))
	    {
	      if (ctype_isDirectBool (ct))
		{
		  if (cstring_equal (e, context_getFalseName ())
		      || cstring_equal (e, context_getTrueName ()))
		    {
		      ;
		    }
		  else
		    {
		      if (optgenerror 
			  (FLG_INCONDEFS,
			   message ("Enumerator member %s declared with "
				    "inconsistent type: %s",
				    e, ctype_unparse (c)),
			   uentry_whereLast (ue)))
			{
			  uentry_showWhereSpecifiedExtra 
			    (ue, cstring_copy (ctype_unparse (ct)));
			}
		    }
		}
	      else
		{
		  if (optgenerror 
		      (FLG_INCONDEFS,
		       message ("Enumerator member %s declared with "
				"inconsistent type: %s",
				e, ctype_unparse (c)),
		       uentry_whereLast (ue)))
		    {
		      uentry_showWhereSpecifiedExtra 
			(ue, cstring_copy (ctype_unparse (ct)));
		    }
		}
	    }
	}
    } end_enumNameList_elements;
}

static /*@dependent@*/ uentryList currentParamList;

void setCurrentParams (/*@dependent@*/ uentryList ue)
{
  currentParamList = ue;
}

void clearCurrentParams (void)
{
    currentParamList = uentryList_undefined;
}

/*
** requires: uentry_isFunction (e)
**           parameter names for current function are in currentParamList
*/

static void enterFunctionParams (uentryList params)
{
  int paramno = 0;

  uentryList_elements (params, current)
    {
      if (uentry_hasName (current)) 
	{
	  uentry_setParamNo (current, paramno);
	  usymtab_supEntry (uentry_copy (current));
	}
      
      paramno++;
    } end_uentryList_elements; 
}
 

extern void enterParamsTemp (void)
{
  usymtab_enterScope ();
  enterFunctionParams (currentParamList);
}

extern void exitParamsTemp (void)
{
  usymtab_quietPlainExitScope ();
}

static /*@exposed@*/ uentry globalDeclareFunction (idDecl tid) 
{
  ctype deftype = idDecl_getCtype (tid);
  ctype rettype;
  uentry ue;
  
  DPRINTF (("Global function: %s", idDecl_unparse (tid)));
  
  if (ctype_isFunction (deftype))
    {
      rettype = ctype_returnValue (deftype);
    }
  else
    {
      rettype = ctype_unknown;
    }

  /*
  ** check has been moved here...
  */

  if (ctype_isFunction (idDecl_getCtype (tid)))
    {
      ue = uentry_makeIdFunction (tid);
      reflectSpecialCode (ue);
      reflectArgsUsed (ue);
    }
  else
    {    
      llparseerror (message ("Inconsistent function declaration: %q",
			     idDecl_unparse (tid)));

      tid = idDecl_replaceCtype 
	(tid, ctype_makeFunction (ctype_unknown, uentryList_undefined));
      ue = uentry_makeIdFunction (tid);
    }
  
  reflectStorageClass (ue);

  uentry_checkParams (ue);
  reflectModGlobs (ue);

  ue = usymtab_supGlobalEntryReturn (ue);
  context_enterFunction (ue);
  enterFunctionParams (uentry_getParams (ue));

  resetStorageClass ();
  return (ue);
}

/*
** for now, no type checking
** (must check later though!)
*/

static /*@only@*/ uentry globalDeclareOldStyleFunction (idDecl tid)
{
  uentry ue;

  /*
  ** check has been moved here...
  */

  if (cstring_equalLit (idDecl_observeId (tid), "main"))
    {
      context_setFlagTemp (FLG_MAINTYPE, FALSE);
    }

  ue = uentry_makeIdFunction (tid);
  reflectStorageClass (ue);
  reflectSpecialCode (ue);
  reflectArgsUsed (ue);
  uentry_setDefined (ue, g_currentloc);

    uentry_checkParams (ue);
  
  if (ProcessingGlobals)
    {
      uentry_setGlobals (ue, currentGlobals);
    }

  resetStorageClass ();
  return (ue);
}

static void oldStyleDeclareFunction (/*@only@*/ uentry e)
{
  uentryList params = saveParamList;
  ctype rt = uentry_getType (e);

  llassert (ctype_isFunction (rt));

  e = usymtab_supGlobalEntryReturn (e);

  context_enterFunction (e);
  enterFunctionParams (params);
  saveParamList = uentryList_undefined;
  resetStorageClass ();
}

void declareFunction (idDecl tid) /*@globals undef saveFunction; @*/
{
  uentry ue;

  DPRINTF (("Declare function: %s", idDecl_unparse (tid)));
  
  if (ProcessingParams)
    {
      ue = globalDeclareOldStyleFunction (tid);
      saveFunction = ue;
    }
  else
    {
      saveFunction = uentry_undefined;

      if (context_inRealFunction ())
	{
	  ue = uentry_makeVariableLoc (idDecl_observeId (tid), ctype_unknown);

	  llparseerror (message ("Function declared inside function: %q",
				 idDecl_unparse (tid)));
	  
	  context_quietExitFunction ();
	  ue = usymtab_supEntryReturn (ue);
	}
      else
	{
	  if (context_inInnerScope ())
	    {
	      llparseerror (message ("Declaration in inner context: %q",
				     idDecl_unparse (tid)));
	      
	      sRef_setGlobalScope ();
	      ue = uentry_makeVariableLoc (idDecl_observeId (tid), 
					   ctype_unknown);
	      ue = usymtab_supGlobalEntryReturn (ue);
	      sRef_clearGlobalScope ();
	    }
	  else
	    {
	      ue = globalDeclareFunction (tid);
	    }
	}
      
      resetGlobals ();
    }

  resetStorageClass ();
  idDecl_free (tid);
}

void declareStaticFunction (idDecl tid) /*@globals undef saveFunction; @*/
{
  uentry ue;

  DPRINTF (("Declare static funciton: %s", idDecl_unparse (tid)));

  if (ProcessingParams)
    {
      ue = globalDeclareOldStyleFunction (tid);
      saveFunction = ue;
    }
  else
    {
      saveFunction = uentry_undefined;

      if (context_inRealFunction ())
	{
	  ue = uentry_makeVariableLoc (idDecl_observeId (tid), ctype_unknown);

	  llparseerror (message ("Function declared inside function: %q",
				 idDecl_unparse (tid)));
	  
	  context_quietExitFunction ();
	  ue = usymtab_supEntryReturn (ue);
	}
      else
	{
	  if (context_inInnerScope ())
	    {
	      llparseerror (message ("Declaration in inner context: %q",
				     idDecl_unparse (tid)));
	      
	      sRef_setGlobalScope ();
	      ue = uentry_makeVariableLoc (idDecl_observeId (tid), 
					   ctype_unknown);
	      ue = usymtab_supGlobalEntryReturn (ue);
	      sRef_clearGlobalScope ();
	    }
	  else
	    {
	      ctype deftype = idDecl_getCtype (tid);
	      ctype rettype;
	      
	      if (ctype_isFunction (deftype))
		{
		  rettype = ctype_returnValue (deftype);
		}
	      else
		{
		  rettype = ctype_unknown;
		}
	      
	      /*
	      ** check has been moved here...
	      */
	      
	      if (ctype_isFunction (idDecl_getCtype (tid)))
		{
		  ue = uentry_makeIdFunction (tid);
		  reflectSpecialCode (ue);
		  reflectArgsUsed (ue);
		}
	      else
		{    
		  llparseerror (message ("Inconsistent function declaration: %q",
					 idDecl_unparse (tid)));
		  
		  tid = idDecl_replaceCtype 
		    (tid, ctype_makeFunction (ctype_unknown, uentryList_undefined));
		  ue = uentry_makeIdFunction (tid);
		}
	      
	      reflectStorageClass (ue);
	      uentry_setStatic (ue);

	      uentry_checkParams (ue);
	      reflectModGlobs (ue);
	
	      DPRINTF (("Sub global entry: %s", uentry_unparse (ue)));
	      ue = usymtab_supGlobalEntryReturn (ue);

	      context_enterFunction (ue);
	      enterFunctionParams (uentry_getParams (ue));
	      resetStorageClass ();
	    }
	}
      
      resetGlobals ();
    }
  
  resetStorageClass ();
  idDecl_free (tid);
}

void
checkTypeDecl (uentry e, ctype rep)
{
  cstring n = uentry_getName (e);

  DPRINTF (("Check type decl: %s", n));

  if (cstring_equal (context_getBoolName (), n))
    {
      ctype rrep = ctype_realType (rep);
      
      /*
      ** for abstract enum types, we need to fix the enum members:
      ** they should have the abstract type, not the rep type.
      */
      
      if (ctype_isEnum (ctype_realType (rrep)))
	{
	  enumNameList el = ctype_elist (rrep);
	  
	  enumNameList_elements (el, ye)
	    {
	      if (usymtab_existsGlob (ye))
		{
		  uentry ue = usymtab_lookupSafe (ye);
		  uentry_setType (ue, ctype_bool);
		}

	      if (cstring_equal (context_getTrueName (), ye)
		  || cstring_equal (context_getFalseName (), ye))
		{
		  ;
		}
	      else
		{
		  vgenhinterror 
		    (FLG_SYNTAX,
		     message ("Member of boolean enumerated type definition "
			      "does not match name set to represent TRUE "
			      "or FALSE: %s",
			      ye),
		     message ("Use -boolfalse and -booltrue to set the "
			      "name of false and true boolean values."),
		     uentry_whereDefined (e));
		}
	    } end_enumNameList_elements;
	}
    }

  if (usymtab_exists (n))
    {
      usymId llm = usymtab_getId (n);
      uentry le  = usymtab_getTypeEntry (llm);

      uentry_setDeclared (e, g_currentloc); 
      uentry_setSref (e, sRef_makeGlobal (llm, uentry_getType (le)));

      DPRINTF (("Here we are: %s / %s",
		n, context_getBoolName ()));
      
      if (uentry_isAbstractDatatype (le))
	{
	  ctype rrep = ctype_realType (rep);

	  /*
	  ** for abstract enum types, we need to fix the enum members:
	  ** they should have the abstract type, not the rep type.
	  */

	  if (ctype_isEnum (ctype_realType (rrep)))
	    {
	      ctype at = uentry_getAbstractType (le);
	      enumNameList el = ctype_elist (rrep);

	      enumNameList_elements (el, ye)
		{
		  if (usymtab_existsGlob (ye))
		    {
		      uentry ue = usymtab_lookupSafe (ye);

		      llassert (uentry_isEitherConstant (ue));
		      llassertprint (ctype_match (uentry_getType (ue), rrep),
				     ("Bad enum: %s / %s",
				      uentry_unparse (ue),
				      ctype_unparse (rrep)));
		      
		      uentry_setType (ue, at);
		    }
		} end_enumNameList_elements;
	    }
	  
	  if (uentry_isMutableDatatype (le))
	    {
	      /* maybe more complicated if abstract and immutable ? */

	      if (!ctype_isRealPointer (rep) && !ctype_isRealAbstract (rep))
		{
		  voptgenerror 
		    (FLG_MUTREP,
		     message ("Mutable abstract type %s declared without pointer "
			      "indirection: %s (violates assignment semantics)",
			      n, ctype_unparse (rep)),
		     uentry_whereDefined (e));
		  
		  uentry_setMutable (e);
		}
	    }
	}
    }
  else
    {
      fileloc fl = uentry_whereDeclared (e);

      if (context_getFlag (FLG_LIKELYBOOL)
	  && !context_getFlag (FLG_BOOLINT))
	{
	  if ((cstring_equalLit (n, "BOOL")
	       || cstring_equalLit (n, "Bool")
	       || cstring_equalLit (n, "bool")
	       || cstring_equalLit (n, "boolean")
	       || cstring_equalLit (n, "Boolean")
	       || cstring_equalLit (n, "BOOLEAN"))
	      && !(cstring_equal (n, context_getBoolName ())))
	    {
	      if (context_setBoolName ()) {
		voptgenerror 
		  (FLG_LIKELYBOOL,
		   message ("Type %s is probably meant as a boolean type, but does "
			    "not match the boolean type name \"%s\".",
			    n,
			    context_getBoolName ()),
		   fl);
	      } else
		voptgenerror 
		  (FLG_LIKELYBOOL,
		   message ("Type %s is probably meant as a boolean type, "
			    "but the boolean type name is not set. "
			    "Use -booltype %s to set it.",
			    n,
			    n),
		   fl);
		}
	}

      if (!uentry_isStatic (e)
	  && !ctype_isFunction (uentry_getType (e)) 
	  && !fileloc_isLib (fl) 
	  && !fileloc_isImport (fl)
	  && fileloc_isHeader (fl))
	{
	  voptgenerror (FLG_EXPORTTYPE,
			message ("Type exported, but not specified: %s\n", n),
			fl);
	}
    }

  cstring_free (n);
}

uentryList
fixUentryList (idDeclList tl, qtype q)
{
  uentryList f = uentryList_new ();
  
  idDeclList_elements (tl, i)
  {
    if (idDecl_isDefined (i))
      {
	uentry ue;
	uentry old;
	ctype rt;

	(void) idDecl_fixBase (i, q);

	/*
	** implicit annotations 
	*/

	(void) fixStructDecl (i);

	ue = uentry_makeIdVariable (i);
	rt = ctype_realType (uentry_getType (ue));

	/*
	** where is this here???

	if (ctype_isArray (rt) || ctype_isSU (rt))
	  {
	    sRef_setAllocated (uentry_getSref (ue), uentry_whereDefined (ue));
	  }

        **
	*/

	if (uentry_isValid (old = uentryList_lookupField (f, uentry_rawName (ue))))
	  {
	    if (optgenerror (FLG_SYNTAX,
			     message ("Field name reused: %s", uentry_rawName (ue)),
			     uentry_whereDefined (ue)))
	      {
		llgenmsg (message ("Previous use of %s", uentry_rawName (ue)),
			  uentry_whereDefined (old));
	      }
	  }
	
	f = uentryList_add (f, ue);
      }
  } end_idDeclList_elements;

  idDeclList_free (tl);
  return (f);
}

/*
** This is a hack to support unnamed struct/union fields as done by
** Microsoft VC++.  It is not supported by the ANSI standard.  
**
** The inner fields are added to the outer structure.  This is meaningful
** for nesting structs inside unions, but lclint does no related 
** checking.
*/

uentryList
fixUnnamedDecl (qtype q)
{
  ctype ct = ctype_realType (qtype_getType (q));

  if (ctype_isStruct (ct) || ctype_isUnion (ct))
    {
      uentryList res = ctype_getFields (ct);

      return (uentryList_copy (res));
    }
  else
    {      
      BADBRANCHCONT;
    }

  return uentryList_undefined;
}

void setStorageClass (storageClassCode sc)
{
  storageClass = sc;
}

void
setProcessingIterVars (uentry iter)
{
  ProcessingIterVars = TRUE;
  currentIter = iter;
  saveIterParamNo = 0;
}

void
setProcessingGlobalsList ()
{
  ProcessingGlobals = TRUE;

  llassert (globSet_isUndefined (currentGlobals));
  currentGlobals = globSet_undefined;

  llassert (sRefSet_isUndefined (fcnModifies));
  fcnModifies = sRefSet_undefined;
  
  /*
  ** No, special clauses might have been processed first!  
  llassert (specialClauses_isUndefined (specClauses));
  specClauses = specialClauses_undefined;
  */

  fcnNoGlobals = FALSE;
}

static bool ProcessingGlobMods = FALSE;

void
setProcessingGlobMods ()
{
  ProcessingGlobMods = TRUE;
}

void
clearProcessingGlobMods ()
{
  ProcessingGlobMods = FALSE;
}

bool
isProcessingGlobMods ()
{
  return (ProcessingGlobMods);
}

static void resetGlobals (void)
{
  ProcessingGlobals = FALSE;
  currentGlobals = globSet_undefined;
  llassert (sRefSet_isUndefined (fcnModifies));
  fcnModifies = sRefSet_undefined;
  fcnNoGlobals = FALSE;
}

void
unsetProcessingGlobals ()
{
  ProcessingGlobals = FALSE;
}

void
setProcessingVars (/*@only@*/ qtype q)
{
  ProcessingVars = TRUE;
  qtype_free (processingType);
  processingType = q;
}

static void
setGenericParamList (/*@dependent@*/ uentryList pm)
{
  ProcessingParams = TRUE;
  saveParamList = pm;
}

void
setProcessingTypedef (/*@only@*/ qtype q)
{
  ProcessingTypedef = TRUE;

  qtype_free (processingType);
  processingType = q;
}

void
unsetProcessingVars ()
{
  resetStorageClass ();
  ProcessingVars = FALSE;
}

void 
doneParams ()
{  
  if (ProcessingParams)
    {
      if (uentry_isInvalid (saveFunction))
	{
	  llbuglit ("unsetProcessingVars: no saved function\n");
	  
	  if (sRefSet_isDefined (fcnModifies)) {
	    sRefSet_free (fcnModifies);
	    fcnModifies = sRefSet_undefined;
	  }
	}
      else
	{
	  ctype ct = ctype_returnValue (uentry_getType (saveFunction));
	  uentryList params = uentryList_copy (saveParamList);
	  ctype ct2 = ctype_makeFunction (ct, params);

	  uentry_setType (saveFunction, ct2);
	  ProcessingParams = FALSE;

	  reflectModGlobs (saveFunction);
	  oldStyleDeclareFunction (saveFunction);
	  saveFunction = uentry_undefined;
	  resetGlobals ();
	}
    }
  else
    {
      /*
      ** If the paramlist used a type name, we could be here.
      */

      llfatalerror (message ("%q: Old-style function parameter list uses a "
			     "type name.", fileloc_unparse (g_currentloc)));
    }
}

void 
checkDoneParams ()
{
  if (uentry_isValid (saveFunction))
    {
      /*
      ** old style declaration
      */

      ctype ct = ctype_returnValue (uentry_getType (saveFunction));
      ctype ct2;

      uentryList_elements (saveParamList, current)
	{
	  uentry_setType (current, ctype_int); /* all params are ints */
	} end_uentryList_elements; 

      ct2 = ctype_makeParamsFunction (ct, uentryList_copy (saveParamList));
      
      uentry_setType (saveFunction, ct2);
      ProcessingParams = FALSE;
      
      oldStyleDeclareFunction (saveFunction);
      saveFunction = uentry_undefined;
    }
}

void
unsetProcessingTypedef ()
{
  ProcessingTypedef = FALSE;
}

void checkConstant (qtype t, idDecl id) 
{
  uentry e;

  id = idDecl_fixBase (id, t);
  e = uentry_makeIdConstant (id);

  reflectStorageClass (e);
  resetStorageClass ();

  usymtab_supGlobalEntry (e);
}

void checkValueConstant (qtype t, idDecl id, exprNode e) 
{
  uentry ue;

  id = idDecl_fixBase (id, t);
  ue = uentry_makeIdConstant (id);
  reflectStorageClass (ue);
  resetStorageClass ();

  if (exprNode_isDefined (e))
    {
      if (!exprNode_matchType (uentry_getType (ue), e))
	{
	  (void) gentypeerror 
	    (exprNode_getType (e), e,
	     uentry_getType (ue), exprNode_undefined,
	     message ("Constant %q initialized to type %t, expects %t: %s",
		      uentry_getName (ue),  
		      exprNode_getType (e), 
		      uentry_getType (ue),
		      exprNode_unparse (e)),
	     exprNode_loc (e));
	}
      else
	{
	  if (exprNode_hasValue (e))
	    {
	      uentry_mergeConstantValue (ue, multiVal_copy (exprNode_getValue (e)));
	    }
	}
    }

  usymtab_supGlobalEntry (ue);
}


void processNamedDecl (idDecl t)
{
  if (qtype_isUndefined (processingType))
    {
      llparseerror (message ("No type before declaration name: %q", idDecl_unparse (t)));

      processingType = qtype_create (ctype_unknown);
    }

  t = idDecl_fixBase (t, processingType);

  DPRINTF (("Declare: %s", idDecl_unparse (t)));

  if (ProcessingGlobals)
    {
      cstring id = idDecl_getName (t);
      uentry ue = usymtab_lookupSafe (id);

      if (!uentry_isValid (ue))
	{
	  llerror (FLG_UNRECOG,
		   message ("Variable used in globals list is undeclared: %s", id));
	}
      else
	{
	  if (!ctype_match (uentry_getType (ue), idDecl_getCtype (t)))
	    {
	      voptgenerror 
		(FLG_INCONDEFS,
		 message ("Variable %s used in globals list declared %s, "
			  "but listed as %s", 
			  id, ctype_unparse (uentry_getType (ue)), 
			  ctype_unparse (idDecl_getCtype (t))),
		 g_currentloc);
	    }
	  else
	    {
	      sRef sr = sRef_copy (uentry_getSref (ue));

	      reflectGlobalQualifiers (sr, idDecl_getQuals (t));

	      currentGlobals = globSet_insert (currentGlobals, sr);
	    }
	}
    }
  else if (ProcessingVars)
    {
      uentry e;
      ctype ct;
      
      ct = ctype_realType (idDecl_getCtype (t));

      if (ProcessingParams)
	{
	  cstring id = idDecl_getName (t);
	  int paramno = uentryList_lookupRealName (saveParamList, id);

	  if (paramno >= 0)
	    {
	      uentry cparam = uentryList_getN (saveParamList, paramno);

	      uentry_setType (cparam, idDecl_getCtype (t));
	      uentry_reflectQualifiers (cparam, idDecl_getQuals (t));
	      uentry_setDeclaredOnly (cparam, context_getSaveLocation ());
	    }
	  else
	    {
	      llfatalerrorLoc
		(message ("Old style declaration uses unlisted parameter: %s", 
			  id));
	    }
	}
      else
	{
	  fileloc loc;

	  if (context_inIterDef ())
	    {
	      cstring pname = makeParam (idDecl_observeId (t));
	      uentry p = usymtab_lookupSafe (pname);

	      cstring_free (pname);

	      if (uentry_isYield (p))
		{
		  e = uentry_makeParam (t, sRef_getParam (uentry_getSref (p)));
		  
		  uentry_checkYieldParam (p, e);
		  
		  usymtab_supEntrySref (e);
		  return;
		}
	    }

	  if ((hasSpecialCode () || argsUsed)
	      && ctype_isFunction (idDecl_getCtype (t)))
	    {
	      e = uentry_makeIdFunction (t);
	      reflectSpecialCode (e);
	      reflectArgsUsed (e);
	    }
	  else
	    {
	      e = uentry_makeIdVariable (t);
	    }

	  loc = uentry_whereDeclared (e);

	  /*
	  if (context_inGlobalScope ())
	    {
	    uentry_checkParams was here!
	    }
	    */

	  if (ctype_isFunction (uentry_getType (e)))
	    {
	      reflectModGlobs (e);
	    }
	  else
	    {
	      llassert (!globSet_isDefined (currentGlobals)
			&& !sRefSet_isDefined (fcnModifies));
	    }
	  
	  e = usymtab_supEntrySrefReturn (e);

	  if (uentry_isExtern (e) && !context_inGlobalScope ())
	    {
	      voptgenerror 
		(FLG_NESTEDEXTERN,
		 message ("Declaration using extern inside function scope: %q",
			  uentry_unparse (e)),
		 g_currentloc);
	      
	      uentry_setDefined (e, fileloc_getExternal ());
	      sRef_setDefined (uentry_getSref (e), fileloc_getExternal ());
	    }

	  if (uentry_isFunction (e))
	    {
	      uentry_checkParams (e);
	      checkParamNames (e);
	    }

	  if (uentry_isVar (e) 
	      && uentry_isCheckedUnknown (e))
	    {
	      sRef sr = uentry_getSref (e);

	      if (sRef_isLocalVar (sr))
		{
		  if (context_getFlag (FLG_IMPCHECKMODINTERNALS))
		    {
		      uentry_setCheckMod (e);
		    }
		  else
		    {
		      uentry_setUnchecked (e);
		    }
		}
	      else if (sRef_isFileStatic (sr))
		{
		  if (context_getFlag (FLG_IMPCHECKEDSTRICTSTATICS))
		    {
		      uentry_setCheckedStrict (e);
		    }
		  else if (context_getFlag (FLG_IMPCHECKEDSTATICS))
		    {
		      uentry_setChecked (e);
		    }
		  else if (context_getFlag (FLG_IMPCHECKMODSTATICS))
		    {
		      uentry_setCheckMod (e);
		    }
		  else
		    {
		      ;
		    }
		}
	      else /* real global */
		{
		  llassert (sRef_isRealGlobal (sr));

		  if (context_getFlag (FLG_IMPCHECKEDSTRICTGLOBALS))
		    {
		      uentry_setCheckedStrict (e);
		    }
		  else if (context_getFlag (FLG_IMPCHECKEDGLOBALS))
		    {
		      uentry_setChecked (e);
		    }
		  else if (context_getFlag (FLG_IMPCHECKMODGLOBALS))
		    {
		      uentry_setCheckMod (e);
		    }
		  else
		    {
		      ;
		    }
		}
	    }
	}
    }
  else if (ProcessingTypedef)
    {
      ctype ct = idDecl_getCtype (t);
      uentry e;

      DPRINTF (("Processing typedef: %s", ctype_unparse (ct)));
      
      e = uentry_makeIdDatatype (t);

      if (cstring_equal (idDecl_getName (t), context_getBoolName ())) {
	ctype rt = ctype_realType (ct);
	
	if (ctype_isEnum (rt)) {
	  ;
	} else {
	  if (!(ctype_isInt (rt)
		|| ctype_isUnknown (rt)
		|| ctype_isChar (rt))) {
	    (void) llgenerror
	      (FLG_BOOLTYPE, 
	       message ("Boolean type %s defined using non-standard type %s (integral, char or enum type expected)",
			context_getBoolName (),
			ctype_unparse (ct)),
	       uentry_whereLast (e));
	  }
	  
	  ct = ctype_bool;
	  uentry_setType (e, ct);
	}
      }

      reflectStorageClass (e);
      checkTypeDecl (e, ct);
      
      e = usymtab_supReturnTypeEntry (e);

      if (uentry_isMaybeAbstract (e))
	{
	  if (context_getFlag (FLG_IMPABSTRACT))
	    {
	      uentry_setAbstract (e);
	    }
	  else
	    {
	      uentry_setConcrete (e);
	    }
	}
    }
  else
    {
      llparseerror (message ("Suspect missing struct or union keyword: %q",
			     idDecl_unparse (t)));
    }

  }

/*
** moved from grammar
*/

static idDecl fixStructDecl (/*@returned@*/ idDecl d)
{
  if (ctype_isVisiblySharable (idDecl_getCtype (d)) 
      && context_getFlag (FLG_STRUCTIMPONLY))
    {
      if (!qualList_hasAliasQualifier (idDecl_getQuals (d)))
	{
	  if (qualList_hasExposureQualifier (idDecl_getQuals (d)))
	    {
	      idDecl_addQual (d, qual_createDependent ());
	    }
	  else
	    {
	      idDecl_addQual (d, qual_createImpOnly ());
	    }
	}
    }

  return d;
}

ctype
declareUnnamedStruct (/*@only@*/ uentryList f)
{
  if (context_maybeSet (FLG_NUMSTRUCTFIELDS))
    {
      int num = uentryList_size (f);
      int max = context_getValue (FLG_NUMSTRUCTFIELDS);

      if (num > max)
	{
	  voptgenerror 
	    (FLG_NUMSTRUCTFIELDS,
	     message ("Structure declared with %d fields "
		      "(limit is set to %d)",
		      num, max),
	     g_currentloc);
	}
    }

  return (ctype_createUnnamedStruct (f));
}

ctype
declareUnnamedUnion (/*@only@*/ uentryList f)
{
  if (context_maybeSet (FLG_NUMSTRUCTFIELDS))
    {
      int num = uentryList_size (f);
      int max = context_getValue (FLG_NUMSTRUCTFIELDS);

      if (num > max)
	{
	  voptgenerror 
	    (FLG_NUMSTRUCTFIELDS,
	     message ("Union declared with %d fields "
		      "(limit is set to %d)",
		      num, max),
	     g_currentloc);
	}
    }

  return (ctype_createUnnamedUnion (f));
}

ctype declareStruct (cstring id, /*@only@*/ uentryList f)
{
  ctype ct; 
  uentry ue;
  int num = uentryList_size (f);

  ct = ctype_createStruct (cstring_copy (id), f);
  ue = uentry_makeStructTagLoc (id, ct);

  if (context_maybeSet (FLG_NUMSTRUCTFIELDS))
    {
      int max = context_getValue (FLG_NUMSTRUCTFIELDS);

      if (num > max)
	{
	  voptgenerror 
	    (FLG_NUMSTRUCTFIELDS,
	     message ("Structure %q declared with %d fields "
		      "(limit is set to %d)",
		      uentry_getName (ue), num, max),
	     uentry_whereLast (ue));
	}
    }

  return (usymtab_supTypeEntry (ue));
}

ctype declareUnion (cstring id, uentryList f)
{
  ctype ct; 
  uentry ue;
  int num = uentryList_size (f);

  ct = ctype_createUnion (cstring_copy (id), f);
  ue = uentry_makeUnionTagLoc (id, ct);

  if (context_maybeSet (FLG_NUMSTRUCTFIELDS))
    {
      int max = context_getValue (FLG_NUMSTRUCTFIELDS);

      if (num > max)
	{
	  voptgenerror 
	    (FLG_NUMSTRUCTFIELDS,
	     message ("Union %q declared with %d fields "
		      "(limit is set to %d)",
		      uentry_getName (ue), num, max),
	     uentry_whereLast (ue));
	}
    }

  return (usymtab_supTypeEntry (ue));
}

ctype handleStruct (/*@only@*/ cstring id)
{
  if (usymtab_existsStructTag (id))
    {
      ctype ct = uentry_getAbstractType (usymtab_lookupStructTag (id));

      cstring_free (id);
      return ct;
    }
  else
    {
      return (ctype_createForwardStruct (id));
    }
}

ctype handleUnion (/*@only@*/ cstring id)
{
  if (usymtab_existsUnionTag (id))
    {
      ctype ret = uentry_getAbstractType (usymtab_lookupUnionTag (id));
      cstring_free (id);
      return (ret);
    }
  else
    {
      return (ctype_createForwardUnion (id));
    }
}

ctype
handleEnum (cstring id)
{
  if (usymtab_existsEnumTag (id))
    {
      ctype ret = uentry_getAbstractType (usymtab_lookupEnumTag (id));
      cstring_free (id);
      return ret;
    }
  else
    {
      return (declareEnum (id, enumNameList_new ()));
    }
}

bool processingIterVars (void) 
{ 
  return ProcessingIterVars; 
}

uentry getCurrentIter (void) 
{
  return currentIter; 
}

static bool flipOldStyle = FALSE;
static bool flipNewStyle = TRUE;

void setFlipOldStyle ()          { flipOldStyle = TRUE; }
bool isFlipOldStyle ()           { return flipOldStyle; }
bool isNewStyle ()               { return flipNewStyle; }
void setNewStyle ()              { flipNewStyle = TRUE; }

/*@dependent@*/ uentryList handleParamIdList (/*@dependent@*/ uentryList params)
{  
  int paramno = 0;

  /*
  ** this is a really YUCKY hack to handle old style
  ** declarations.
  */
  
  voptgenerror (FLG_OLDSTYLE,
		cstring_makeLiteral ("Old style function declaration"),
		g_currentloc); 

  uentryList_elements (params, current)
    {
      uentry_setParam (current);
      uentry_setSref (current, sRef_makeParam (paramno, ctype_unknown));
      paramno++;
    } end_uentryList_elements;

  setGenericParamList (params);
  g_expectingTypeName = TRUE; 

  return params;
}

/*@dependent@*/ uentryList handleParamTypeList (/*@returned@*/ uentryList params)
{
  if (flipOldStyle)
    {
      uentryList_fixMissingNames (params);

      voptgenerror (FLG_OLDSTYLE, 
		    cstring_makeLiteral ("Old style function declaration."), 
		    g_currentloc); 
      
      setGenericParamList (params);
      flipOldStyle = FALSE;
      g_expectingTypeName = TRUE; 
    }
 
  return (params); 
}

void
doVaDcl ()
{
  ctype c = ctype_unknown;
  cstring id = cstring_makeLiteral ("va_alist");
  uentry e;

  if (ProcessingParams)
    {
      int i = uentryList_lookupRealName (saveParamList, id);
      
      if (i >= 0)
	{
	  e = uentry_makeVariableSrefParam (id, c, sRef_makeParam (i, c));
	}
      else
	{
	  e = uentry_undefined; /* suppress gcc message */
	  llfatalerrorLoc (cstring_makeLiteral ("va_dcl used without va_alist"));
	}
    }
  else
    {	 
      llerror (FLG_SYNTAX, cstring_makeLiteral ("va_dcl used outside of function declaration"));
      e = uentry_makeVariableLoc (id, c);
    }

  cstring_free (id);
  uentry_setUsed (e, g_currentloc);  
  usymtab_supEntrySref (e);
}

/*@exposed@*/ sRef modListPointer (sRef s)
{
  ctype ct = sRef_getType (s);
  ctype rt = ctype_realType (ct);
  
  if (ctype_isAP (rt))
    {
      if (context_inHeader () && ctype_isAbstract (ct))
	{
	  voptgenerror 
	    (FLG_ABSTRACT,
	     message
	     ("Modifies clause in header file dereferences abstract "
	      "type %s (interface modifies clause should not depend "
	      "on or expose type representation): %q",
	      ctype_unparse (ct),
	      sRef_unparse (s)),
	     g_currentloc);
	}

      return (sRef_constructPointer (s));
    }
  else
    {
      if (ctype_isKnown (rt))
	{
	  voptgenerror 
	    (FLG_TYPE,
	     message ("Implementation modifies clause dereferences non-pointer (type %s): %q",
		      ctype_unparse (rt),
		      sRef_unparse (s)),
	     g_currentloc);
	}

      return s;
    }
}

/*@exposed@*/ sRef modListFieldAccess (sRef s, cstring f)
{
  ctype ct = sRef_getType (s);
  ctype rt = ctype_realType (ct);
  
  if (ctype_isStructorUnion (rt))
    {
      uentry tf = uentryList_lookupField (ctype_getFields (rt), f);
      
      if (uentry_isUndefined (tf))
	{
	  voptgenerror (FLG_TYPE,
			message ("Modifies list accesses non-existent "
				 "field %s of %t: %q", f, ct, 
				 sRef_unparse (s)),
			g_currentloc);
	  
	  cstring_free (f);
	  return sRef_undefined;
	}
      else 
	{
	  if (ctype_isAbstract (ct) && context_inHeader ())
	    {
	      voptgenerror 
		(FLG_ABSTRACT,
		 message
		 ("Modifies clause in header file accesses abstract "
		  "type %s (interface modifies clause should not depend "
		  "on or expose type representation): %q",
		  ctype_unparse (ct),
		  sRef_unparse (s)),
		 g_currentloc);
	    }
	}
      
      cstring_markOwned (f);
      return (sRef_makeField (s, f));
    }
  else
    {
      voptgenerror 
	(FLG_TYPE,
	 message ("Modifies clause dereferences non-pointer (type %s): %q",
		  ctype_unparse (rt),
		  sRef_unparse (s)),
	 g_currentloc);
      
      cstring_free (f);
      return s;
    }
}

sRef globListUnrecognized (cstring s)
{
  if (cstring_equalLit (s, "nothing"))
    {
      return sRef_makeNothing ();
    }
  else if (cstring_equalLit (s, "internalState"))
    {
      return sRef_makeInternalState ();
    }
  else if (cstring_equalLit (s, "fileSystem")
	   || cstring_equalLit (s, "systemState"))
    {
      return sRef_makeSystemState ();
    }
  else
    {
      voptgenerror 
	(FLG_UNRECOG, 
	 message ("Unrecognized identifier in globals list: %s", s), 
	 g_currentloc);
      
      return sRef_undefined;
    }
}

/*@exposed@*/ sRef modListArrowAccess (sRef s, cstring f)
{
  ctype ct = sRef_getType (s);
  ctype rt = ctype_realType (ct);

  if (ctype_isRealPointer (rt))
    {
      ctype b = ctype_baseArrayPtr (rt);
      ctype rb = ctype_realType (b);

      if (ctype_isStructorUnion (rb))
	{
	  uentry tf = uentryList_lookupField (ctype_getFields (rb), f);
      
	  if (uentry_isUndefined (tf))
	    {
	      voptgenerror (FLG_TYPE,
			    message ("Modifies list arrow accesses non-existent "
				     "field %s of %t: %q", f, b, 
				     sRef_unparse (s)),
			    g_currentloc);
	      
	      cstring_free (f);
	      return sRef_undefined;
	    }
	  else 
	    {
	      if (context_inHeader ())
		{
		  if (ctype_isAbstract (b))
		    {
		      voptgenerror 
			(FLG_ABSTRACT,
			 message
			 ("Modifies clause in header file arrow accesses abstract "
			  "type %s (interface modifies clause should not depend "
			  "on or expose type representation): %q",
			  ctype_unparse (b),
			  sRef_unparse (s)),
			 g_currentloc);
		    }
		}
	      else 
		{
		  if (ctype_isAbstract (ct))
		    {
		      voptgenerror 
			(FLG_ABSTRACT,
			 message
			 ("Modifies clause in header file arrow accesses abstract "
			  "type %s (interface modifies clause should not depend "
			  "on or expose type representation): %q",
			  ctype_unparse (ct),
			  sRef_unparse (s)),
			 g_currentloc);
		    }
		}
	    }

	  cstring_markOwned (f);
	  return (sRef_makeArrow (s, f));
	}
      else
	{
	  voptgenerror 
	    (FLG_TYPE,
	     message ("Modifies clause arrow accesses pointer to "
		      "non-structure (type %s): %q",
		      ctype_unparse (rt),
		      sRef_unparse (s)),
	     g_currentloc);
	}
    }
  else
    {
      voptgenerror 
	(FLG_TYPE,
	 message ("Modifies clause arrow accesses non-pointer (type %s): %q",
		  ctype_unparse (rt),
		  sRef_unparse (s)),
	 g_currentloc);
    }

  cstring_free (f);
  return s;
}

sRef checkSpecClausesId (uentry ue)
{
  cstring s = uentry_rawName (ue);

  if (sRef_isGlobal (uentry_getSref (ue)))
    {
      voptgenerror 
	(FLG_SYNTAX, 
	 message ("Global variable %s used special clause.  (Global variables "
		  "are not recognized in special clauses.  If there is "
		  "sufficient interest in support for this, it may be "
		  "added to a future release.  Send mail to "
		  "lclint@cs.virginia.edu.)",
		  s),
	 g_currentloc);
      
      return sRef_undefined;
    }
  else
    {
      if (cstring_equalLit (s, "result"))
	{
	  if (optgenerror 
	      (FLG_SYNTAX, 
	       message ("Special clause list uses %s which is a variable and has special "
			"meaning in a modifies list.  (Special meaning assumed.)", s), 
	       g_currentloc))
	    {
	      uentry_showWhereDeclared (ue);
	    }
	}

      return uentry_getSref (ue);
    }
}


void checkModifiesId (uentry ue)
{
  cstring s = uentry_rawName (ue);

  if (cstring_equalLit (s, "nothing")
      || cstring_equalLit (s, "internalState")
      || cstring_equalLit (s, "systemState")
      || (cstring_equalLit (s, "fileSystem")))
    {
      if (optgenerror 
	  (FLG_SYNTAX, 
	   message ("Modifies list uses %s which is a variable and has special "
		    "meaning in a modifies list.  (Special meaning assumed.)", s), 
	   g_currentloc))
	{
	  uentry_showWhereDeclared (ue);
	}
    }
}

/*@exposed@*/ sRef fixModifiesId (cstring s) 
{
  sRef ret;
  cstring pname = makeParam (s);
  uentry ue = usymtab_lookupSafe (pname);

  cstring_free (pname);

  if (cstring_equalLit (s, "nothing"))
    {
      ret = sRef_makeNothing ();
    }
  else if (cstring_equalLit (s, "internalState"))
    {
      ret = sRef_makeInternalState ();
    }
  else if (cstring_equalLit (s, "fileSystem")
	   || cstring_equalLit (s, "systemState"))
    {
      ret = sRef_makeSystemState ();
    }
  else
    {
      ret = sRef_undefined;
    }

  if (sRef_isValid (ret))
    {
      if (uentry_isValid (ue))
	{
	  voptgenerror 
	    (FLG_SYNTAX, 
	     message ("Modifies list uses %s which is a parameter and has special "
		      "meaning in a modifies list.  (Special meaning assumed.)", s), 
	     g_currentloc);
	}
    }
  else
    {
      if (uentry_isValid (ue))
	{
	  ret = uentry_getSref (ue);
	}
      else
	{
	  fileloc loc = fileloc_decColumn (g_currentloc, cstring_length (s));
	  ret = sRef_undefined;

	  voptgenerror 
	    (FLG_UNRECOG, 
	     message ("Unrecognized identifier in modifies comment: %s", s), 
	     loc);

	  fileloc_free (loc);
	}
    }
  
  return ret;
}

sRef fixSpecClausesId (cstring s) 
{
  sRef ret;
  cstring pname = makeParam (s);
  uentry ue = usymtab_lookupSafe (pname);

  cstring_free (pname);

  if (cstring_equalLit (s, "result"))
    {
      ret = sRef_makeResult ();
    }
  else
    {
      ret = sRef_undefined;
    }

  if (sRef_isValid (ret))
    {
      if (uentry_isValid (ue))
	{
	  voptgenerror 
	    (FLG_SYNTAX, 
	     message ("Special clause uses %s which is a parameter and has special "
		      "meaning in a special clause.  (Special meaning assumed.)", s), 
	     g_currentloc);
	}
    }
  else
    {
      if (uentry_isValid (ue))
	{
	  ret = uentry_getSref (ue);

	  if (sRef_isGlobal (ret))
	    {
	      voptgenerror 
		(FLG_SYNTAX, 
		 message ("Global variable %s used special clause.  (Global variables "
			  "are not recognized in special clauses.  If there is "
			  "sufficient interest in support for this, it may be "
			  "added to a future release.  Send mail to "
			  "lclint@cs.virginia.edu.)",
			  s), 
		 g_currentloc);
	      
	      ret = sRef_undefined;
	    }
	}
      else
	{
	  fileloc loc = fileloc_decColumn (g_currentloc, cstring_length (s));
	  ret = sRef_undefined; 
	  
	  voptgenerror 
	    (FLG_UNRECOG, 
	     message ("Unrecognized identifier in special clause: %s", s), 
	     loc);

	  fileloc_free (loc);
	}
    }
  
  return ret;
}

sRef modListArrayFetch (sRef s, /*@unused@*/ sRef mexp)
{
  ctype ct = sRef_getType (s);
  ctype rt = ctype_realType (ct);

  if (ctype_isAP (rt))
    {
      if (context_inHeader () && ctype_isAbstract (ct))
	{
	  voptgenerror 
	    (FLG_ABSTRACT,
	     message
	     ("Modifies clause in header file indexes abstract "
	      "type %s (interface modifies clause should not depend "
	      "on or expose type representation): %q",
	      ctype_unparse (ct),
	      sRef_unparse (s)),
	     g_currentloc);
	}
      
      return (sRef_makeAnyArrayFetch (s));
    }
  else
    {
      voptgenerror
	(FLG_TYPE,
	 message
	 ("Implementation modifies clause uses array fetch on non-array (type %s): %q",
	  ctype_unparse (ct), sRef_unparse (s)),
	 g_currentloc);
      return s;
    }
}







