/*
** Copyright (C) University of Virginia, Massachusetts Institue of Technology 1994-2000.
** See ../LICENSE for license information.
**
*/
/*
** exprNode.h
*/

# ifndef EXPRNODE_H
# define EXPRNODE_H

/*
** expression Nodes:
**
**     ctype typ --- type of expression
**
**     union _val
**      { long    ival;
**        char    cval;
**        double  fval;
**        cstring sval;
**      }   *val --- value, if known.  if unknown, val = (_val *)0
**
**     storeRef sref --- storage referred to by expression, of storeRef_null
**
**     cstring  etext --- to get nice error messages, punt for now!
*/

/* in exprNode_type: typedef struct _exprNode *exprNode; */

typedef enum 
{ 
  XPR_PARENS,     XPR_ASSIGN,  XPR_CALL,     XPR_EMPTY,    XPR_VAR, 
  XPR_OP,         XPR_POSTOP,  XPR_PREOP,    XPR_SIZEOFT,  XPR_SIZEOF, XPR_ALIGNOFT, XPR_ALIGNOF,
  XPR_OFFSETOF,   XPR_CAST,    XPR_FETCH,    XPR_VAARG,    XPR_ITER,  
  XPR_FOR,        XPR_FORPRED, XPR_GOTO,     XPR_CONTINUE, XPR_BREAK, 
  XPR_RETURN,     XPR_NULLRETURN, XPR_COMMA, XPR_COND,     XPR_IF,  XPR_IFELSE,
  XPR_DOWHILE,    XPR_WHILE,   XPR_STMT,     XPR_STMTLIST, XPR_SWITCH,
  XPR_INIT,       XPR_FACCESS, XPR_ARROW,    XPR_CONST,    XPR_STRINGLITERAL, 
  XPR_NUMLIT,     XPR_BODY,    XPR_NODE,     XPR_ITERCALL, XPR_TOK,
  XPR_WHILEPRED,  XPR_CASE,    XPR_FTCASE,   XPR_DEFAULT,  XPR_FTDEFAULT,
  XPR_BLOCK,      XPR_INITBLOCK, XPR_LABEL
} exprKind; 

typedef struct _exprOffsetof
{
  /*@only@*/ qtype q;
  /*@only@*/ cstringList field;
} *exprOffsetof;

typedef struct _exprPair
{
  /*@only@*/ exprNode a;
  /*@only@*/ exprNode b;
} *exprPair;

typedef struct _exprTriple
{
  /*@only@*/ exprNode pred;
  /*@only@*/ exprNode tbranch;
  /*@only@*/ exprNode fbranch;
} *exprTriple;

typedef struct _exprIter
{
  /*@dependent@*/ /*@observer@*/ uentry sname;
  /*@only@*/    exprNodeList args;
  /*@only@*/    exprNode body;
  /*@dependent@*/ /*@observer@*/ uentry ename;
} *exprIter;

typedef struct _exprCall
{
  /*@only@*/ exprNode fcn;
  /*@only@*/ exprNodeList args;
} *exprCall;

typedef struct _exprIterCall
{
  /*@dependent@*/ /*@exposed@*/ uentry iter;
  /*@only@*/   exprNodeList args;
} *exprIterCall;

typedef struct _exprOp
{
  /*@only@*/ exprNode a;
  /*@only@*/ exprNode b;
  lltok      op;
} *exprOp;

typedef struct _exprField
{
  /*@only@*/ exprNode rec;
  /*@only@*/ cstring  field;
} *exprField;

typedef struct _exprUop
{
  /*@only@*/ exprNode a;
  lltok      op;
} *exprUop;

typedef struct _exprCast
{
  /*@only@*/ exprNode exp;
	     lltok    tok;
	     qtype    q;
} *exprCast;

typedef struct _exprInit
{
  /*@only@*/ exprNode exp;
	     idDecl   id;
} *exprInit;

typedef /*@null@*/ union _exprData
{
  cstring literal;
  cstring id;
  lltok *tok;
  qtype qt;    /* sizeof(type) */
  /* use for any 2-operator (comma, arrayFetch, case, stmt) */
  exprPair pair; 
  exprOp op;    /* pair + operator */
  exprUop uop;
  exprInit init;
  exprIter iter;
  exprCall call;
  exprIterCall itercall;
  exprCast cast;
  exprNode single;
  exprField field;
  exprTriple triple; /* ifelse, ternary op, for pred */
  exprOffsetof offset;
} *exprData;

/*@constant null exprData exprData_undefined; @*/
# define exprData_undefined  ((exprData) NULL)

struct _exprNode
{
  bool isJumpPoint BOOLBITS; /* expr can be reached non-sequentially */
  bool canBreak BOOLBITS;    /* expr can break (has break, continue) */
  bool mustBreak BOOLBITS;

  ctype typ;
  exitkind exitCode;    

  multiVal val;
  /*@exposed@*/ sRef sref;
  sRefSet uses;   /* sRef's used by this expression */
  sRefSet sets;   /* sRef's set by this expression */
  sRefSet msets;  /* sRef's possibly set (implicit out params, etc.) */

  guardSet guards;
  exprKind kind;

  fileloc loc;
  /*@relnull@*/ exprData edata;
  cstring etext;
} ;

/*@constant null exprNode exprNode_undefined; @*/
# define exprNode_undefined ((exprNode)NULL)

extern /*@falsenull@*/ bool exprNode_isDefined (exprNode p_e) /*@*/ ;
extern /*@unused@*/ /*@truenull@*/ bool exprNode_isUndefined (exprNode p_e) /*@*/ ;
extern /*@truenull@*/ bool exprNode_isError (exprNode p_e) /*@*/ ;

# define exprNode_isDefined(e)        ((e) != exprNode_undefined)
# define exprNode_isUndefined(e)      ((e) == exprNode_undefined)
# define exprNode_isError(e)          ((e) == exprNode_undefined)

extern /*@dependent@*/ /*@exposed@*/ guardSet 
  exprNode_getGuards (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_getGuards(e) \
  (exprNode_isDefined(e) ? (e)->guards : guardSet_undefined)

extern ctype exprNode_getType (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_getType(e) \
  (exprNode_isDefined(e) ? (e)->typ : ctype_unknown)

extern /*@unused@*/ /*@falsenull@*/ bool exprNode_isInParens (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_isInParens(e) \
  (exprNode_isDefined(e) && (e)->kind == XPR_PARENS)

extern bool exprNode_isStringLiteral (/*@sef@*/ exprNode p_e);
# define exprNode_isStringLiteral(e) \
  (exprNode_isDefined(e) && (e)->kind == XPR_STRINGLITERAL)

extern /*@unused@*/ bool exprNode_knownIntValue (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_knownIntValue(e) \
  (exprNode_isDefined(e) && multiVal_isInt (exprNode_getValue (e)))

extern /*@unused@*/ bool exprNode_knownStringValue (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_knownStringValue(e) \
  (exprNode_isDefined(e) && multiVal_isString (exprNode_getValue (e)))

extern bool exprNode_hasValue (/*@sef@*/ exprNode p_e) /*@*/ ;
# define exprNode_hasValue(e) \
  (exprNode_isDefined(e) && multiVal_isDefined (exprNode_getValue (e)))

extern /*@exposed@*/ multiVal exprNode_getValue (exprNode p_e) /*@*/ ;

extern /*@observer@*/ cstring exprNode_unparseFirst (exprNode p_e) /*@*/ ;
extern /*@observer@*/ guardSet exprNode_getForGuards (exprNode p_pred) /*@*/ ;
extern bool exprNode_isNullValue (exprNode p_e) /*@*/ ;
extern /*@exposed@*/ sRef exprNode_getSref (exprNode p_e) /*@*/ ;
extern /*@observer@*/ uentry exprNode_getUentry (exprNode p_e) 
   /*@globals internalState@*/ ;
extern void exprNode_produceGuards (exprNode p_pred) /*@modifies p_pred@*/ ;
extern /*@observer@*/ fileloc exprNode_loc (exprNode p_e) /*@*/ ;
extern exprNode
  exprNode_charLiteral (char p_c, cstring p_text, /*@only@*/ fileloc p_loc) /*@*/ ;
extern /*@observer@*/ exprNode exprNode_makeMustExit (void) /*@*/ ;
extern exprNode 
  exprNode_cond (/*@keep@*/ exprNode p_pred, /*@keep@*/ exprNode p_ifclause, 
		 /*@keep@*/ exprNode p_elseclause) /*@*/ ;
extern exprNode exprNode_makeError(void) /*@*/ ;

extern exprNode exprNode_makeInitBlock (lltok p_brace, /*@only@*/ exprNodeList p_inits) /*@*/ ;

extern exprNode exprNode_functionCall (/*@only@*/ exprNode p_f, 
				       /*@only@*/ exprNodeList p_args) /*@*/ ;
extern /*@notnull@*/ exprNode 
  exprNode_fromIdentifier (/*@observer@*/ uentry p_c) /*@globals internalState@*/ ;
extern exprNode exprNode_fromUIO (cstring p_c) /*@globals internalState@*/ ;
extern exprNode exprNode_fieldAccess (/*@only@*/ exprNode p_s, /*@only@*/ cstring p_f) /*@*/ ;
extern exprNode exprNode_arrowAccess (/*@only@*/ exprNode p_s, /*@only@*/ cstring p_f) /*@*/ ;
extern exprNode exprNode_postOp (/*@only@*/ exprNode p_e, /*@only@*/ lltok p_op) 
          /*@modifies p_e@*/ ;
extern exprNode exprNode_preOp (/*@only@*/ exprNode p_e, /*@only@*/ lltok p_op) /*@*/ ;
extern exprNode exprNode_addParens (/*@only@*/ lltok p_lpar, /*@only@*/ exprNode p_e) /*@*/ ;
extern exprNode exprNode_offsetof (/*@only@*/ qtype p_qt, /*@only@*/ cstringList p_s) /*@*/ ;
extern exprNode exprNode_sizeofType (/*@only@*/ qtype p_qt) /*@*/ ;
extern exprNode exprNode_sizeofExpr (/*@only@*/ exprNode p_e) /*@*/ ;
extern exprNode exprNode_alignofType (/*@only@*/ qtype p_qt) /*@*/ ;
extern exprNode exprNode_alignofExpr (/*@only@*/ exprNode p_e) /*@*/ ;
extern exprNode 
  exprNode_op (/*@only@*/ exprNode p_e1, /*@keep@*/ exprNode p_e2, /*@only@*/ lltok p_op) /*@*/ ;
extern exprNode 
  exprNode_assign (/*@only@*/ exprNode p_e1, /*@only@*/ exprNode p_e2, /*@only@*/ lltok p_op) ;
extern exprNode
  exprNode_arrayFetch (/*@only@*/ exprNode p_e1, /*@only@*/ exprNode p_e2) 
  /*@modifies p_e1, p_e2@*/ ;

extern void exprNode_free (/*@only@*/ exprNode p_e) ;
extern exprNode
  exprNode_vaArg (/*@only@*/ lltok p_tok, /*@only@*/ exprNode p_arg, /*@only@*/ qtype p_qt) 
  /*@globals internalState@*/ ;

extern exprNode 
  exprNode_stringLiteral (/*@only@*/ cstring p_t, /*@only@*/ fileloc p_loc) /*@*/ ;
extern exprNode exprNode_comma (/*@only@*/ exprNode p_e1, /*@only@*/ exprNode p_e2)  /*@*/ ;
extern exprNode exprNode_labelMarker (/*@only@*/ cstring p_label);
extern exprNode 
  exprNode_notReached (/*@returned@*/ exprNode p_stmt);

extern
exprNode exprNode_caseMarker (/*@only@*/ exprNode p_test, bool p_fallThrough) /*@*/ ;

extern exprNode exprNode_concat (/*@only@*/ exprNode p_e1, /*@only@*/ exprNode p_e2);
extern exprNode exprNode_createTok (/*@only@*/ lltok p_t) /*@*/ ;
extern exprNode exprNode_statement (/*@only@*/ exprNode p_e);
extern exprNode exprNode_makeBlock (/*@only@*/ exprNode p_e);
extern exprNode exprNode_if (/*@only@*/ exprNode p_pred, /*@only@*/ exprNode p_tclause);
extern exprNode 
  exprNode_ifelse (/*@only@*/ exprNode p_pred, /*@only@*/ exprNode p_tclause, 
		   /*@only@*/ exprNode p_eclause);
extern exprNode exprNode_switch (/*@only@*/ exprNode p_e, /*@only@*/ exprNode p_s);
extern exprNode exprNode_while (/*@keep@*/ exprNode p_t, /*@keep@*/ exprNode p_b);
extern exprNode exprNode_doWhile (/*@only@*/ exprNode p_b, /*@only@*/ exprNode p_t);
extern /*@notnull@*/ /*@only@*/ exprNode exprNode_goto (/*@only@*/ cstring p_label);
extern exprNode exprNode_continue (/*@only@*/ lltok p_l, int p_qcontinue);
extern exprNode exprNode_break (/*@only@*/ lltok p_l, int p_bqual);
extern exprNode exprNode_nullReturn (/*@only@*/ lltok p_t);
extern exprNode exprNode_return (/*@only@*/ exprNode p_e);
extern /*@dependent@*/ /*@observer@*/ cstring 
  exprNode_unparse (exprNode p_e) /*@*/ ; 

extern bool exprNode_isCharLit (exprNode p_e) /*@*/ ;
extern bool exprNode_isNumLit (exprNode p_e) /*@*/ ;

extern exprNode 
  exprNode_makeInitialization (/*@only@*/ idDecl p_t, /*@only@*/ exprNode p_e);
extern bool exprNode_matchType (ctype p_expected, exprNode p_e);

extern /*@notnull@*/ /*@only@*/ exprNode 
  exprNode_defaultMarker (/*@only@*/ lltok p_def, bool p_fallThrough);

extern exprNode 
  exprNode_iter (/*@observer@*/ uentry p_name, /*@only@*/ exprNodeList p_alist, 
		 /*@only@*/ exprNode p_body, /*@observer@*/ uentry p_end);
extern exprNode exprNode_iterId (/*@observer@*/ uentry p_c);
extern            exprNode exprNode_iterExpr (/*@returned@*/ exprNode p_e);
extern exprNode exprNode_iterNewId (/*@only@*/ cstring p_s);
extern exprNode 
  exprNode_iterStart (/*@observer@*/ uentry p_name, /*@only@*/ exprNodeList p_alist);
extern exprNode 
  exprNode_numLiteral (ctype p_c, /*@temp@*/ cstring p_t, 
		       /*@only@*/ fileloc p_loc, long p_val); 
extern void exprNode_initMod (void) /*@modifies internalState@*/ ;
extern exprNode exprNode_for (/*@keep@*/ exprNode p_inc, /*@keep@*/ exprNode p_body);
extern exprNode 
  exprNode_forPred (/*@only@*/ exprNode p_init, 
		    /*@only@*/ exprNode p_test, /*@only@*/ exprNode p_inc);
extern exprNode 
  exprNode_floatLiteral (double p_d, ctype p_ct, 
			 cstring p_text, /*@only@*/ fileloc p_loc);
extern /*@notnull@*/ exprNode exprNode_createId (/*@observer@*/ uentry p_c);
extern exprNode exprNode_cast (/*@only@*/ lltok p_tok, /*@only@*/ exprNode p_e, /*@only@*/ qtype p_q);
extern bool exprNode_matchLiteral (ctype p_expected, exprNode p_e);
extern void exprNode_checkUseParam (exprNode p_current);
extern void exprNode_checkSet (exprNode p_e, sRef p_s);
extern void exprNode_checkMSet (exprNode p_e, sRef p_s);
extern exprNode exprNode_checkExpr (/*@returned@*/ exprNode p_e);
extern bool exprNode_mustEscape (exprNode p_e);
extern bool exprNode_errorEscape (exprNode p_e);
extern bool exprNode_mayEscape (exprNode p_e);
extern exprNode exprNode_whilePred (/*@only@*/ exprNode p_test);
extern exprNode 
  exprNode_updateLocation (/*@returned@*/ exprNode p_e, /*@temp@*/ fileloc p_loc);
extern void exprNode_freeShallow (/*@only@*/ exprNode p_e);
extern void exprNode_destroyMod (void) /*@modifies internalState@*/ ;
extern bool exprNode_isAssign (exprNode p_e) /*@*/ ;

/*@-exportlocal@*/
extern bool exprNode_isDefaultMarker (exprNode p_e) /*@*/ ;
extern bool exprNode_isCaseMarker (exprNode p_e) /*@*/ ;
extern bool exprNode_isLabelMarker (exprNode p_e) /*@*/ ;
/*@=exportlocal@*/

# else
# error "Multiple include"
# endif
















