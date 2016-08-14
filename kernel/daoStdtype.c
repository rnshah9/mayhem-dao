/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2016, Limin Fu
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED  BY THE COPYRIGHT HOLDERS AND  CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED  WARRANTIES,  INCLUDING,  BUT NOT LIMITED TO,  THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL  THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,
// INDIRECT,  INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSEQUENTIAL  DAMAGES (INCLUDING,
// BUT NOT LIMITED TO,  PROCUREMENT OF  SUBSTITUTE  GOODS OR  SERVICES;  LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY OF
// LIABILITY,  WHETHER IN CONTRACT,  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include"stdlib.h"
#include"stdio.h"
#include"string.h"
#include"ctype.h"
#include"math.h"
#include"locale.h"

#include"daoType.h"
#include"daoVmspace.h"
#include"daoNamespace.h"
#include"daoNumtype.h"
#include"daoStream.h"
#include"daoRoutine.h"
#include"daoObject.h"
#include"daoProcess.h"
#include"daoGC.h"
#include"daoStdlib.h"
#include"daoClass.h"
#include"daoParser.h"
#include"daoRegex.h"
#include"daoTasklet.h"
#include"daoValue.h"



void DaoValue_Init( void *value, char type )
{
	DaoNone *self = (DaoNone*) value;
	self->type = type;
	self->subtype = self->trait = self->marks = 0;
	self->refCount = 0;
	if( type >= DAO_ENUM ) ((DaoValue*)self)->xGC.cycRefCount = 0;
}

DaoType* DaoValue_CheckGetField( DaoType *self, DValue *field, DaoRoutine *ctx )
{
	DaoValue *value = DaoType_FindValue( type, field );
	if( value ) return DaoNamespace_GetType( ns, value );
	return NULL;
}

DaoValue* DaoValue_DoGetField( DaoValue *self, DValue *field, DaoProcess *proc )
{
	DaoType *type = DaoNamespace_GetType( proc->activeNamespace, self );
	return DaoType_FindValue( type, field );
}

static int DaoType_CheckTypeRange( DaoType *self, int min, int max )
{
	if( self->tid >= min && self->tid <= max ) return 1;
	if( self->tid & DAO_ANY ) return 1;
	if( self->tid == DAO_VARIANT ){
		int i;
		for(i=0; i<self->nested->size; ++i){
			DaoType *itype = self->nested->items.pType[i];
			if( DaoType_CheckTypeRange( itype, min, max ) ) return 1;
		}
	}
	return 0;
}

int DaoType_CheckNumberIndex( DaoType *self )
{
	return DaoType_CheckTypeRange( self, DAO_BOOLEAN, DAO_FLOAT );
}

int DaoType_CheckRangeIndex( DaoType *self )
{
	if( self->type != DAO_RANGE ) return 0;
	if( ! DaoType_CheckRange( self->nested->items.pType[0], DAO_NONE, DAO_FLOAT ) ) return 0;
	if( ! DaoType_CheckRange( self->nested->items.pType[1], DAO_NONE, DAO_FLOAT ) ) return 0;
	return 1;
}

static daoint Dao_CheckNumberIndex( daoint index, daoint size, DaoProcess *proc )
{
	if( index < 0 ) index += size;
	if( index >= 0 && index < size ) return index;
	DaoProcess_RaiseError( proc, "Index::Range", NULL );
	return -1;
}

typedef struct DIndexRange  DIndexRange;
struct DIndexRange
{
	daoint pos;
	daoint end;
};

static DIndexRange Dao_CheckRangeIndex( DaoRange *range, daoint size, DaoProcess *proc )
{
	DIndexRange res = {-1, -1};
	daoint pos, end;

	if( range->first->type  > DAO_FLOAT ) return res;
	if( range->second->type > DAO_FLOAT ) return res;

	pos = DaoValue_GetInteger( range->first );
	end = DaoValue_GetInteger( range->second );
	if( range->second->type == DAO_NONE ) end = size;

	pos = Dao_CheckNumberIndex( pos, size, proc );
	end = Dao_CheckNumberIndex( end, size + 1, proc ); /* Open index; TODO: check other places; */
	if( pos < 0 || end < 0 ) return res;
	if( pos > end ){
		DaoProcess_RaiseError( proc, "Index::Range", NULL );
		return res;
	}
	res.pos = pos;
	res.end = end;
	return res;
}

static void DaoValue_QuotedPrint( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoTypeCore *core = DaoValue_GetTypeCore( self );
	if( self->type == DAO_STRING ) DaoStream_WriteChar( stream, '"' );
	if( core->Print ) core->Print( self, stream, cycmap, proc );
	if( self->type == DAO_STRING ) DaoStream_WriteChar( stream, '"' );
}

static DaoTuple* DaoProcess_PrepareTuple( DaoProcess *self, DaoType *type, int size )
{
	DaoTuple *tuple = NULL;

	if( size < (type->nested->size - type->variadic) ) return NULL;
	if( type->variadic == 0 ) size = type->nested->size;

	tuple = DaoProcess_NewTuple( self, size );
	GC_Assign( & tuple->ctype, type );
	return tuple;
}




/*
// None value type:
*/
DaoNone* DaoNone_New()
{
	DaoNone *self = (DaoNone*) dao_malloc( sizeof(DaoNone) );
	memset( self, 0, sizeof(DaoNone) );
	self->type = DAO_NONE;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

static void DaoNone_Delete( DaoValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	dao_free( self );
}

static int DaoNone_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid == DAO_NONE && right->tid == DAO_NONE ) return DAO_OK;
	return DAO_ERROR; /* Other cases should be handled by other types; */
}

static int DaoNone_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	if( left->type == DAO_NONE && right->type == DAO_NONE ) return 0;
	return left->type == DAO_NONE ? -1 : 1; /* None value is less than anything else; */
}

static DaoType* DaoNone_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoNone_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	switch( type->tid ){
	case DAO_NONE    : return dao_none_value;
	case DAO_BOOLEAN : num->xBoolean.value = 0; return num;
	case DAO_INTEGER : num->xInteger.value = 0; return num;
	case DAO_FLOAT   : num->xFloat.value = 0.0; return num;
	case DAO_COMPLEX : num->xComplex.value.real = num->xComplex.value.imag = 0.0; return num;
	break;
	}
	if( type->tid == DAO_STRING ) DaoProcess_PutChars( proc, "none" );
	return NULL; /* The VM will handle the case where no value is converted and returned; */
}

static void DaoNone_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteChars( stream, "none[0x0]" );
}

DaoTypeCore daoNoneCore =
{
	"none",                                          /* name */
	{ NULL },                                        /* bases */
	NULL,                                            /* numbers */
	NULL,                                            /* methods */
	NULL,                     NULL,                  /* GetField */
	NULL,                     NULL,                  /* SetField */
	NULL,                     NULL,                  /* GetItem */
	NULL,                     NULL,                  /* SetItem */
	NULL,                     NULL,                  /* Unary */
	NULL,                     NULL,                  /* Binary */
	DaoNone_CheckComparison,  DaoNone_DoComparison,  /* Comparison */
	DaoNone_CheckConversion,  DaoNone_DoConversion,  /* Conversion */
	NULL,                     NULL,                  /* ForEach */
	DaoNone_Print,                                   /* Print */
	NULL,                                            /* Slice */
	NULL,                                            /* Copy */
	DaoNone_Delete,                                  /* Delete */
	NULL                                             /* HandleGC */
};

static DaoNone none = {DAO_NONE,0,DAO_VALUE_CONST,0,1};
DaoValue *dao_none_value = (DaoValue*) (void*) & none;





/*
// Boolean type:
*/
DaoBoolean* DaoBoolean_New( dao_boolean value )
{
	DaoBoolean *self = (DaoBoolean*) dao_malloc( sizeof(DaoBoolean) );
	DaoValue_Init( self, DAO_BOOLEAN );
	self->value = value != 0;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
dao_boolean DaoBoolean_Get( DaoBoolean *self )
{
	return self->value;
}
void DaoBoolean_Set( DaoBoolean *self, dao_boolean value )
{
	self->value = value != 0;
}


static void DaoBoolean_Delete( DaoValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	dao_free( self );
}

static DaoType* DaoBoolean_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code == DVM_NOT ) return dao_type_bool;
	if( op->code == DVM_SIZE ) return dao_type_int;
	return NULL;
}

static DaoValue* DaoBoolean_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	/*
	// Returning NULL without putting a value on the stack will be detected
	// as an error by DaoProcess;
	*/
	switch( op->code ){
	case DVM_NOT  : DaoProcess_PutBoolean( proc, ! value->xBoolean.value ); break;
	case DVM_SIZE : DaoProcess_PutInteger( proc, sizeof(dao_boolean) );     break;
	default: break;
	}
	return NULL;
}

static DaoType* DaoBoolean_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	switch( op->code ){
	case DVM_AND : case DVM_OR  :
	case DVM_LT  : case DVM_LE  :
	case DVM_EQ  : case DVM_NE  :
		if( left->tid <= DAO_BOOLEAN && right->tid <= DAO_BOOLEAN ) return dao_type_bool;
		break;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoBoolean_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	dao_boolean A, B, C = 0;

	if( left->type != DAO_BOOLEAN || right->type != DAO_BOOLEAN ) return NULL;

	A = left->xBoolean.value  != 0;
	B = right->xBoolean.value != 0;

	switch( op->code ){
	case DVM_AND : C = A && B; break;
	case DVM_OR  : C = A || B; break;
	case DVM_LT  : C = A <  B; break;
	case DVM_LE  : C = A <= B; break;
	case DVM_EQ  : C = A == B; break;
	case DVM_NE  : C = A != B; break;
	default: return NULL;
	}
	if( compound ){
		left->xBoolean.value = C;
		return left;
	}
	DaoProcess_PutBoolean( proc, C );
	return NULL;
}

static int DaoBoolean_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid <= DAO_BOOLEAN && right->tid <= DAO_BOOLEAN ) return DAO_OK;
	return DAO_ERROR; /* Other cases should be handled by other types; */
}

static int DaoBoolean_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	dao_boolean A = left->xBoolean.value;
	dao_boolean B = right->xBoolean.value;
	if( A == B ) return 0;
	return A < B ? -1 : 1;
}

static DaoType* DaoBoolean_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoBoolean_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	int bl = self->xBoolean.value;
	switch( type->tid ){
	case DAO_BOOLEAN : num->xBoolean.value = bl; return num;
	case DAO_INTEGER : num->xInteger.value = bl; return num;
	case DAO_FLOAT   : num->xFloat.value   = bl; return num;
	case DAO_COMPLEX : num->xComplex.value.real = bl; num->xComplex.value.imag = 0.0; return num;
	break;
	}
	if( type->tid == DAO_STRING ) DaoProcess_PutChars( proc, bl ? "true" : "false" );
	return NULL; /* The VM will handle the case where no value is converted and returned; */
}

static void DaoBoolean_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteChars( stream, self->xBoolean.value ? "true" : "false" );
}

DaoTypeCore daoBooleanCore =
{
	"bool",                                                /* name */
	{ NULL },                                              /* bases */
	NULL,                                                  /* numbers */
	NULL,                                                  /* methods */
	NULL,                        NULL,                     /* GetField */
	NULL,                        NULL,                     /* SetField */
	NULL,                        NULL,                     /* GetItem */
	NULL,                        NULL,                     /* SetItem */
	DaoBoolean_CheckUnary,       DaoBoolean_DoUnary,       /* Unary */
	DaoBoolean_CheckBinary,      DaoBoolean_DoBinary,      /* Binary */
	DaoBoolean_CheckComparison,  DaoBoolean_DoComparison,  /* Comparison */
	DaoBoolean_CheckConversion,  DaoBoolean_DoConversion,  /* Conversion */
	NULL,                        NULL,                     /* ForEach */
	DaoBoolean_Print,                                      /* Print */
	NULL,                                                  /* Slice */
	NULL,                                                  /* Copy */
	DaoBoolean_Delete,                                     /* Delete */
	NULL                                                   /* HandleGC */
};

static DaoBoolean dao_false = {DAO_BOOLEAN,0,DAO_VALUE_CONST,0,1,0};
static DaoBoolean dao_true  = {DAO_BOOLEAN,0,DAO_VALUE_CONST,0,1,1};
DaoValue *dao_false_value = (DaoValue*) (void*) & dao_false;
DaoValue *dao_true_value  = (DaoValue*) (void*) & dao_true;





/*
// Integer type:
*/
DaoInteger* DaoInteger_New( dao_integer value )
{
	DaoInteger *self = (DaoInteger*) dao_malloc( sizeof(DaoInteger) );
	DaoValue_Init( self, DAO_INTEGER );
	self->value = value;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

dao_integer DaoInteger_Get( DaoInteger *self )
{
	return self->value;
}

void DaoInteger_Set( DaoInteger *self, dao_integer value )
{
	self->value = value;
}

static void DaoInteger_Delete( DaoValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	dao_free( self );
}

static DaoType* DaoInteger_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	switch( op->code ){
	case DVM_NOT   : return dao_type_bool;
	case DVM_MINUS :
	case DVM_TILDE :
	case DVM_SIZE  : return dao_type_int;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoInteger_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	/*
	// Returning NULL without putting a value on the stack will be detected
	// as an error by DaoProcess;
	*/
	switch( op->code ){
	case DVM_NOT   : DaoProcess_PutBoolean( p, ! value->xInteger.value ); break;
	case DVM_MINUS : DaoProcess_PutInteger( p, - value->xInteger.value ); break;
	case DVM_TILDE : DaoProcess_PutInteger( p, ~ value->xInteger.value ); break;
	case DVM_SIZE  : DaoProcess_PutInteger( p, sizeof(dao_integer) );     break;
	default: break;
	}
	return NULL;
}

static DaoType* DaoInteger_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
	case DVM_MUL : case DVM_DIV :
	case DVM_MOD : case DVM_POW :
	case DVM_BITAND :
	case DVM_BITOR  :
		if( left->tid == DAO_NONE || right->tid == DAO_NONE ) return NULL;
		if( left->tid <= DAO_INTEGER && right->tid <= DAO_INTEGER ) return dao_type_int;
		break;
	case DVM_AND : case DVM_OR :
	case DVM_LT  : case DVM_LE :
	case DVM_EQ  : case DVM_NE :
		if( left->tid <= DAO_INTEGER && right->tid <= DAO_INTEGER ) return dao_type_bool;
		break;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoInteger_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	dao_integer A = 0, B = 0, C = 0;
	int retbool = 0;

	switch( left->type ){
	case DAO_NONE    : A = 0; break;
	case DAO_BOOLEAN : A = left->xBoolean.value; break;
	case DAO_INTEGER : A = left->xInteger.value; break;
	default: return NULL;
	}

	switch( right->type ){
	case DAO_NONE    : B = 0; break;
	case DAO_BOOLEAN : B = right->xBoolean.value; break;
	case DAO_INTEGER : B = right->xInteger.value; break;
	default: return NULL;
	}

	switch( op->code ){
	case DVM_ADD    : C = A + B; break;
	case DVM_SUB    : C = A - B; break;
	case DVM_MUL    : C = A * B; break;
	case DVM_DIV    : if( B == 0 ) goto ErrorDivByZero; C = A / B; break;
	case DVM_MOD    : if( B == 0 ) goto ErrorDivByZero; C = A % B; break;
	case DVM_POW    : C = pow( A, B ); break;
	case DVM_BITAND : C = A & B; break;
	case DVM_BITOR  : C = A | B; break;
	case DVM_AND    : C = A && B; retbool = 1; break;
	case DVM_OR     : C = A || B; retbool = 1; break;
	case DVM_LT     : C = A <  B; retbool = 1; break;
	case DVM_LE     : C = A <= B; retbool = 1; break;
	case DVM_EQ     : C = A == B; retbool = 1; break;
	case DVM_NE     : C = A != B; retbool = 1; break;
	default: return NULL;
	}
	if( retbool ){
		DaoProcess_PutBoolean( proc, C );
	}else{
		DaoProcess_PutInteger( proc, C );
	}
	return NULL;

ErrorDivByZero:
	DaoProcess_RaiseError( self, "Float::DivByZero", "" );
	return NULL;
}

static int DaoInteger_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid <= DAO_INTEGER && right->tid <= DAO_INTEGER ) return DAO_OK;
	return DAO_ERROR; /* Other cases should be handled by other types; */
}

static int DaoInteger_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	dao_integer A = 0, B = 0;

	switch( left->type ){
	case DAO_NONE    : A = 0; break;
	case DAO_BOOLEAN : A = left->xBoolean.value; break;
	case DAO_INTEGER : A = left->xInteger.value; break;
	default: return NULL;
	}

	switch( right->type ){
	case DAO_NONE    : B = 0; break;
	case DAO_BOOLEAN : B = right->xBoolean.value; break;
	case DAO_INTEGER : B = right->xInteger.value; break;
	default: return NULL;
	}
	if( A == B ) return 0;
	return A < B ? -1 : 1;
}

static DaoType* DaoInteger_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoInteger_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	int val = self->xInteger.value;
	switch( type->tid ){
	case DAO_BOOLEAN : num->xBoolean.value = val != 0; return num;
	case DAO_INTEGER : num->xInteger.value = val; return num;
	case DAO_FLOAT   : num->xFloat.value   = val; return num;
	case DAO_COMPLEX : num->xComplex.value.real = val; num->xComplex.value.imag = 0.0; return num;
	break;
	}
	if( type->tid == DAO_STRING ){
		char chs[100] = {0};

		sprintf( chs, "%"DAO_I64, (long long) self->xInteger.value );
		DaoProcess_PutChars( proc, chs );
	}
	return NULL; /* The VM will handle the case where no value is converted and returned; */
}

static void DaoInteger_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteInt( stream, self->xInteger.value );
}

DaoTypeCore daoIntegerCore =
{
	"int",                                                 /* name */
	{ NULL },                                              /* bases */
	NULL,                                                  /* numbers */
	NULL,                                                  /* methods */
	NULL,                        NULL,                     /* GetField */
	NULL,                        NULL,                     /* SetField */
	NULL,                        NULL,                     /* GetItem */
	NULL,                        NULL,                     /* SetItem */
	DaoInteger_CheckUnary,       DaoInteger_DoUnary,       /* Unary */
	DaoInteger_CheckBinary,      DaoInteger_DoBinary,      /* Binary */
	DaoInteger_CheckComparison,  DaoInteger_DoComparison,  /* Comparison */
	DaoInteger_CheckConversion,  DaoInteger_DoConversion,  /* Conversion */
	NULL,                        NULL,                     /* ForEach */
	DaoInteger_Print,                                      /* Print */
	NULL,                                                  /* Slice */
	NULL,                                                  /* Copy */
	DaoInteger_Delete,                                     /* Delete */
	NULL                                                   /* HandleGC */
};





/*
// Float type:
*/
DaoFloat* DaoFloat_New( dao_float value )
{
	DaoFloat *self = (DaoFloat*) dao_malloc( sizeof(DaoFloat) );
	DaoValue_Init( self, DAO_FLOAT );
	self->value = value;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
dao_float DaoFloat_Get( DaoFloat *self )
{
	return self->value;
}
void DaoFloat_Set( DaoFloat *self, dao_float value )
{
	self->value = value;
}

static void DaoFloat_Delete( DaoValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	dao_free( self );
}

static DaoType* DaoFloat_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	switch( op->code ){
	case DVM_MINUS : return dao_type_float;
	case DVM_SIZE  : return dao_type_int;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoFloat_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	/*
	// Returning NULL without putting a value on the stack will be detected
	// as an error by DaoProcess;
	*/
	switch( op->code ){
	case DVM_MINUS : DaoProcess_PutFloat( proc, - value->xFloat.value ); break;
	case DVM_SIZE  : DaoProcess_PutInteger( proc, sizeof(dao_float) );     break;
	default: break;
	}
	return NULL;
}

static DaoType* DaoFloat_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
	case DVM_MUL : case DVM_DIV :
	case DVM_MOD : case DVM_POW :
		if( left->tid == DAO_NONE || right->tid == DAO_NONE ) return NULL;
		if( left->tid <= DAO_FLOAT && right->tid <= DAO_FLOAT ) return dao_type_int;
		break;
	case DVM_LT  : case DVM_LE :
	case DVM_EQ  : case DVM_NE :
		if( left->tid <= DAO_FLOAT && right->tid <= DAO_FLOAT ) return dao_type_bool;
		break;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoFloat_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	double A = 0.0, B = 0.0, C = 0.0;
	int retbool = 0, D = 0;

	switch( left->type ){
	case DAO_NONE    : A = 0.0; break;
	case DAO_BOOLEAN : A = left->xBoolean.value; break;
	case DAO_INTEGER : A = left->xInteger.value; break;
	case DAO_FLOAT   : A = left->xFloat.value;   break;
	default: return NULL;
	}

	switch( right->type ){
	case DAO_NONE    : B = 0.0; break;
	case DAO_BOOLEAN : B = right->xBoolean.value; break;
	case DAO_INTEGER : B = right->xInteger.value; break;
	case DAO_FLOAT   : B = right->xFloat.value;   break;
	default: return NULL;
	}

	switch( op->code ){
	case DVM_ADD : C = A + B; break;
	case DVM_SUB : C = A - B; break;
	case DVM_MUL : C = A * B; break;
	case DVM_DIV : if( B == 0 ) goto ErrorDivByZero; C = A / B; break;
	case DVM_MOD : if( B == 0 ) goto ErrorDivByZero; C = A - B * (dao_integer)(A/B);; break;
	case DVM_POW : C = pow( A, B ); break;
	case DVM_LT  : D = A <  B; retbool = 1; break;
	case DVM_LE  : D = A <= B; retbool = 1; break;
	case DVM_EQ  : D = A == B; retbool = 1; break;
	case DVM_NE  : D = A != B; retbool = 1; break;
	default: return NULL;
	}
	if( retbool ){
		DaoProcess_PutBoolean( proc, D );
	}else{
		DaoProcess_PutFloat( proc, C );
	}
	return NULL;

ErrorDivByZero:
	DaoProcess_RaiseError( self, "Float::DivByZero", "" );
	return NULL;
}

static int DaoFloat_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid <= DAO_FLOAT && right->tid <= DAO_FLOAT ) return DAO_OK;
	return DAO_ERROR; /* Other cases should be handled by other types; */
}

static int DaoFloat_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	double A = 0.0, B = 0.0;

	switch( left->type ){
	case DAO_NONE    : A = 0.0; break;
	case DAO_BOOLEAN : A = left->xBoolean.value; break;
	case DAO_INTEGER : A = left->xInteger.value; break;
	case DAO_FLOAT   : A = left->xFloat.value;   break;
	default: return NULL;
	}

	switch( right->type ){
	case DAO_NONE    : B = 0.0; break;
	case DAO_BOOLEAN : B = right->xBoolean.value; break;
	case DAO_INTEGER : B = right->xInteger.value; break;
	case DAO_FLOAT   : B = right->xFloat.value;   break;
	default: return NULL;
	}
	if( A == B ) return 0;
	return A < B ? -1 : 1;
}

static DaoType* DaoFloat_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoFloat_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	dao_float val = self->xFloat.value;
	switch( type->tid ){
	case DAO_BOOLEAN : num->xBoolean.value = val != 0; return num;
	case DAO_INTEGER : num->xInteger.value = val; return num;
	case DAO_FLOAT   : num->xFloat.value   = val; return num;
	case DAO_COMPLEX : num->xComplex.value.real = val; num->xComplex.value.imag = 0.0; return num;
	break;
	}
	if( type->tid == DAO_STRING ){
		char chs[100] = {0};

		sprintf( chs, "%g", self->xFloat.value );
		DaoProcess_PutChars( proc, chs );
	}
	return NULL;
}

static void DaoFloat_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteFloat( stream, self->xFloat.value );
}

DaoTypeCore daoFloatCore =
{
	"float",                                           /* name */
	{ NULL },                                          /* bases */
	NULL,                                              /* numbers */
	NULL,                                              /* methods */
	NULL,                        NULL,                 /* GetField */
	NULL,                        NULL,                 /* SetField */
	NULL,                        NULL,                 /* GetItem */
	NULL,                        NULL,                 /* SetItem */
	DaoFloat_CheckUnary,       DaoFloat_DoUnary,       /* Unary */
	DaoFloat_CheckBinary,      DaoFloat_DoBinary,      /* Binary */
	DaoFloat_CheckComparison,  DaoFloat_DoComparison,  /* Comparison */
	DaoFloat_CheckConversion,  DaoFloat_DoConversion,  /* Conversion */
	NULL,                      NULL,                   /* ForEach */
	DaoFloat_Print,                                    /* Print */
	NULL,                                              /* Slice */
	NULL,                                              /* Copy */
	DaoFloat_Delete,                                   /* Delete */
	NULL                                               /* HandleGC */
};





/*
// Complex type:
*/
DaoComplex* DaoComplex_New( dao_complex value )
{
	DaoComplex *self = (DaoComplex*) dao_malloc( sizeof(DaoComplex) );
	DaoValue_Init( self, DAO_COMPLEX );
	self->value = value;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
DaoComplex* DaoComplex_New2( dao_float real, dao_float imag )
{
	DaoComplex *self = (DaoComplex*) dao_malloc( sizeof(DaoComplex) );
	DaoValue_Init( self, DAO_COMPLEX );
	self->value.real = real;
	self->value.imag = imag;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
dao_complex  DaoComplex_Get( DaoComplex *self )
{
	return self->value;
}
void DaoComplex_Set( DaoComplex *self, dao_complex value )
{
	self->value = value;
}

static void DaoComplex_Delete( DaoValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	dao_free( self );
}

static DaoType* DaoComplex_CheckGetField( DaoType *self, DString *field, DaoRoutine *ctx )
{
	if( strcmp( field->chars, "real" ) == 0 ){
		return dao_type_float;
	}else if( strcmp( field->chars, "imag" ) == 0 ){
		return dao_type_float;
	}
	return NULL;
}

static DaoValue* DaoComplex_DoGetField( DaoValue *self, DString *field, DaoProcess *proc )
{
	dao_complex value = self->xComplex.value;
	if( strcmp( field->chars, "real" ) == 0 ){
		DaoProcess_PutFloat( proc, value.real );
	}else if( strcmp( field->chars, "imag" ) == 0 ){
		DaoProcess_PutFloat( proc, value.imag );
	}
	return NULL;
}

static int DaoComplex_CheckSetField( DaoType *self, DString *field, DaoType *value, DaoRoutine *ctx )
{
	if( strcmp( field->chars, "real" ) != 0 && strcmp( field->chars, "imag" ) != 0 ){
		return DAO_ERROR_FIELD;
	}
	if( value->tid > DAO_FLOAT ) return DAO_ERROR_VALUE;
	return DAO_OK;
}

static int DaoComplex_DoSetField( DaoValue *self, DString *field, DaoValue *value, DaoProcess *proc )
{
	double A = 0.0;

	switch( right->type ){
	case DAO_NONE    : A = 0.0; break;
	case DAO_BOOLEAN : A = value->xBoolean.value; break;
	case DAO_INTEGER : A = value->xInteger.value; break;
	case DAO_FLOAT   : A = value->xFloat.value;   break;
	default: return 0;
	}
	if( strcmp( field->chars, "real" ) == 0 ){
		self->xComplex.value.real = A;
	}else if( strcmp( field->chars, "imag" ) == 0 ){
		self->xComplex.value.imag = A;
	}else{
		return 1;
	}
	return 0;
}

static DaoType* DaoComplex_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	if( N == 0 ) return self;
	if( N != 1 ) return NULL;
	if( DaoType_CheckNumberIndex( index[0] ) ) return dao_type_float;
	return NULL;
}

static DaoValue* DaoComplex_DoGetItem( DaoValue *self, DaoValue *index[], int N, DaoProcess *proc )
{
	dao_complex value = self->xComplex.value;
	dao_integer pos = 0;

	if( N == 0 ) return self;
	if( N != 1 ) return NULL;
	if( index[0]->type > DAO_FLOAT ) return NULL;

	switch( index[0]->type ){
	case DAO_BOOLEAN : pos = index[0]->xBoolean.value; break;
	case DAO_INTEGER : pos = index[0]->xInteger.value; break;
	case DAO_FLOAT   : pos = index[0]->xFloat.value; break;
	default: return NULL;
	}

	switch( pos ){
	case 0 : DaoProcess_PutFloat( proc, value.real ); break;
	case 1 : DaoProcess_PutFloat( proc, value.imag ); break;
	}
	return NULL;
}

static int DaoComplex_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	if( N != 1 ) return DAO_ERROR_INDEX;
	if( index[0]->tid > DAO_FLOAT ) return DAO_ERROR_INDEX;
	if( ! DaoType_CheckNumberIndex( index[0] ) ) return DAO_ERROR_INDEX;
	if( value->tid > DAO_FLOAT ) return DAO_ERROR_VALUE;
	return DAO_OK;
}

static int DaoComplex_DoSetItem( DaoValue *self, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	dao_integer pos = 0;
	double A = 0.0;

	if( N != 1 ) return DAO_ERROR_INDEX;
	if( index[0]->type > DAO_FLOAT ) return DAO_ERROR_INDEX;

	switch( index[0]->type ){
	case DAO_NONE    : pos = 0; break;
	case DAO_BOOLEAN : pos = index[0]->xBoolean.value; break;
	case DAO_INTEGER : pos = index[0]->xInteger.value; break;
	case DAO_FLOAT   : pos = index[0]->xFloat.value; break;
	default: return DAO_ERROR_INDEX;
	}

	switch( right->type ){
	case DAO_NONE    : A = 0.0; break;
	case DAO_BOOLEAN : A = value->xBoolean.value; break;
	case DAO_INTEGER : A = value->xInteger.value; break;
	case DAO_FLOAT   : A = value->xFloat.value;   break;
	default: return DAO_ERROR_VALUE;
	}

	switch( pos ){
	case 0 : self->xComplex.value.real = A; break;
	case 1 : self->xComplex.value.imag = A; break;
	default : return DAO_ERROR_INDEX_RANGE;
	}
	return DAO_OK;
}

static DaoType* DaoComplex_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	switch( op->code ){
	case DVM_MINUS : return dao_type_complex;
	case DVM_TILDE : return dao_type_complex;
	case DVM_SIZE  : return dao_type_int;
	default: break;
	}
	return NULL;
}

static dao_complex dao_complex_new( dao_float real, dao_float imag )
{
	dao_complex com;
	com.real = real;
	com.imag = imag;
	return com;
}

static DaoValue* DaoComplex_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	dao_complex com = value->xComplex.value;

	switch( op->code ){
	case DVM_MINUS : DaoProcess_PutComplex( proc, dao_complex_new( -com.real, -com.imag ) ); break;
	case DVM_TILDE : DaoProcess_PutComplex( proc, dao_complex_new( com.real, -com.imag ) ); break;
	case DVM_SIZE  : DaoProcess_PutInteger( proc, sizeof(dao_complex) );     break;
	default: break;
	}
	return NULL;
}

static DaoType* DaoComplex_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
	case DVM_MUL : case DVM_DIV :
		if( left->tid == DAO_NONE || right->tid == DAO_NONE ) return NULL;
		if( left->tid <= DAO_COMPLEX && right->tid <= DAO_COMPLEX ) return dao_type_int;
		break;
	case DVM_EQ : case DVM_NE :
		if( left->tid <= DAO_COMPLEX && right->tid <= DAO_COMPLEX ) return dao_type_bool;
		break;
	case DVM_POW : break; /* XXX: pow for complex??? */
	default: break;
	}
	return NULL;
}

static DaoValue* DaoComplex_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	dao_complex C = { 0.0, 0.0 };
	int retbool = 0, D = 0;

	if( left->type == DAO_COMPLEX && right->type == DAO_COMPLEX ){
		double AR = left->xComplex.value.real;
		double AI = left->xComplex.value.imag;
		double BR = right->xComplex.value.real;
		double BI = right->xComplex.value.imag;
		double N = 0;
		switch( vmc->code ){
		case DVM_ADD :
			C.real = AR + BR;
			C.imag = AI + BI;
			break;
		case DVM_SUB :
			C.real = AR - BR;
			C.imag = AI - BI;
			break;
		case DVM_MUL :
			C.real = AR * BR - AI * BI;
			C.imag = AR * BI + AI * BR;
			break;
		case DVM_DIV :
			N = BR * BR + BI * BI;
			if( N == 0.0 ) goto ErrorDivByZero;
			C.real = (AR * BR + AI * BI) / N;
			C.imag = (AR * BI - AI * BR) / N;
			break;
		case DVM_EQ :
			D = (AR == BR) && (AI == BI);
			retbool = 1;
			break;
		case DVM_EQ :
			D = (AR != BR) || (AI != BI);
			retbool = 1;
			break;
		default: return NULL;
		}
	}else if( left->type == DAO_COMPLEX ){
		double AR = left->xComplex.value.real;
		double AI = left->xComplex.value.imag;
		double B = 0.0;

		switch( right->type ){
		case DAO_NONE    : B = 0.0; break;
		case DAO_BOOLEAN : B = right->xBoolean.value; break;
		case DAO_INTEGER : B = right->xInteger.value; break;
		case DAO_FLOAT   : B = right->xFloat.value;   break;
		default: return NULL;
		}

		switch( vmc->code ){
		case DVM_DIV :
			if( B == 0.0 ) goto ErrorDivByZero;
			C.real = AR / B;
			C.imag = AI / B;
			break;
		case DVM_ADD : C.real = AR + B; C.imag = AI; break;
		case DVM_SUB : C.real = AR - B; C.imag = AI; break;
		case DVM_MUL : C.real = AR * B; C.imag = AI * B; break;
		case DVM_EQ  : D = (AR == B) && (AI == 0.0); retbool = 1; break;
		case DVM_NE  : D = (AR != B) || (AI != 0.0); retbool = 1; break;
		default: return NULL;
		}
	}else{
		double BR = right->xComplex.value.real;
		double BI = right->xComplex.value.imag;
		double A = 0.0;
		double N;

		switch( left->type ){
		case DAO_NONE    : A = 0.0; break;
		case DAO_BOOLEAN : A = left->xBoolean.value; break;
		case DAO_INTEGER : A = left->xInteger.value; break;
		case DAO_FLOAT   : A = left->xFloat.value;   break;
		default: return NULL;
		}

		switch( vmc->code ){
		case DVM_DIV :
			N = BR * BR + BI * BI;
			if( N == 0.0 ) goto ErrorDivByZero;
			C.real = A * BR / N;
			C.imag = A * BI / N;
			break;
		case DVM_ADD : C.real = A + BR; C.imag =   BI; break;
		case DVM_SUB : C.real = A - BR; C.imag = - BI; break;
		case DVM_MUL : C.real = A * BR; C.imag = A * BI; break;
		case DVM_EQ  : D = (A == BR) && (BI == 0.0); retbool = 1; break;
		case DVM_NE  : D = (A != BR) || (BI != 0.0); retbool = 1; break;
		default: return NULL;
		}
	}

	if( retbool ){
		DaoProcess_PutBoolean( proc, D );
	}else{
		DaoProcess_PutComplex( proc, C );
	}
	return NULL;

ErrorDivByZero:
	DaoProcess_RaiseError( proc, "Float::DivByZero", "" );
	return NULL;
}

static DaoType* DaoComplex_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid == DAO_COMPLEX || type->tid == DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoComplex_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	if( type->tid == DAO_COMPLEX ) return self;

	if( type->tid == DAO_STRING ){
		char chs[100] = {0};

		DaoStream_WriteFloat( stream, self->xComplex.value.real );
		if( self->xComplex.value.imag >= -0.0 ) DaoStream_WriteChars( stream, "+" );
		DaoStream_WriteFloat( stream, self->xComplex.value.imag );
		DaoStream_WriteChars( stream, "C" );
		DaoProcess_PutChars( proc, chs );
	}
	return NULL;
}

static void DaoComplex_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteFloat( stream, self->xFloat.value );
}

DaoTypeCore daoComplexCore =
{
	"complex",                                             /* name */
	{ NULL },                                              /* bases */
	NULL,                                                  /* numbers */
	NULL,                                                  /* methods */
	DaoComplex_CheckGetField,    DaoComplex_DoGetField,    /* GetField */
	DaoComplex_CheckSetField,    DaoComplex_DoSetField,    /* SetField */
	DaoComplex_CheckGetItem,     DaoComplex_DoGetItem,     /* GetItem */
	DaoComplex_CheckSetItem,     DaoComplex_DoSetItem,     /* SetItem */
	DaoComplex_CheckUnary,       DaoComplex_DoUnary,       /* Unary */
	DaoComplex_CheckBinary,      DaoComplex_DoBinary,      /* Binary */
	NULL,                        NULL,                     /* Comparison */
	DaoComplex_CheckConversion,  DaoComplex_DoConversion,  /* Conversion */
	NULL,                        NULL,                     /* ForEach */
	DaoComplex_Print,                                      /* Print */
	NULL,                                                  /* Slice */
	NULL,                                                  /* Copy */
	DaoComplex_Delete,                                     /* Delete */
	NULL                                                   /* HandleGC */
};





/*
// String type:
*/
DaoString* DaoString_New()
{
	DaoString *self = (DaoString*) dao_malloc( sizeof(DaoString) );
	DaoValue_Init( self, DAO_STRING );
	self->value = DString_New();
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
DaoString* DaoString_NewChars( const char *mbs )
{
	DaoString *self = DaoString_New();
	DString_SetChars( self->value, mbs );
	return self;
}
DaoString* DaoString_NewBytes( const char *bytes, daoint n )
{
	DaoString *self = DaoString_New();
	DString_SetBytes( self->value, bytes, n );
	return self;
}
DaoString* DaoString_Copy( DaoString *self )
{
	DaoString *copy = (DaoString*) dao_malloc( sizeof(DaoString) );
	DaoValue_Init( copy, DAO_STRING );
	copy->value = DString_Copy( self->value );
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) copy );
#endif
	return copy;
}
void DaoString_Delete( DaoString *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	DString_Delete( self->value );
	dao_free( self );
}
daoint  DaoString_Size( DaoString *self )
{
	return self->value->size;
}
DString* DaoString_Get( DaoString *self )
{
	return self->value;
}
const char* DaoString_GetChars( DaoString *self )
{
	return DString_GetData( self->value );
}

void DaoString_Set( DaoString *self, DString *str )
{
	DString_Assign( self->value, str );
}
void DaoString_SetChars( DaoString *self, const char *mbs )
{
	DString_SetChars( self->value, mbs );
}
void DaoString_SetBytes( DaoString *self, const char *bytes, daoint n )
{
	DString_SetBytes( self->value, bytes, n );
}


static DaoType* DaoString_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	if( N == 0 ) return self;
	if( N != 1 ) return NULL;
	if( index[0]->tid == DAO_ITERATOR ){
		if( DaoType_CheckNumberIndex( index[0]->nested->items.pType[0] ) ) return dao_type_int;
	}else if( index[0]->tid == DAO_RANGE ){
		if( DaoType_CheckRangeIndex( index[0] ) ) return dao_type_string;
	}else{
		if( DaoType_CheckNumberIndex( index[0] ) ) return dao_type_int;
	}
	return NULL;
}

static DaoValue* DaoString_DoGetItem( DaoValue *self, DaoValue *index[], int N, DaoProcess *proc )
{
	daoint pos, end, size = self->xString.value->size;
	DIndexRange range;

	if( N == 0 ) return self;
	if( N != 1 ) return NULL;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return NULL;
		DaoProcess_PutInteger( p, self->xString.value->chars[pos] );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		DaoProcess_PutBytes( proc, self->xString.value->chars + range.pos, range.end - range.pos );
		break;
	case DAO_ITERATOR :
		if( index[0]->xIterator.values[1]->type != DAO_INTEGER ) return NULL;
		pos = Dao_CheckNumberIndex( index[0]->xIterator.values[1]->xInteger.value, size, proc );
		index[0]->xIterator.values[0]->xBoolean.value = (pos + 1) < size;
		index[0]->xIterator.values[1]->xInteger.value = pos + 1;
		if( pos < 0 ) return NULL;
		DaoProcess_PutInteger( proc, self->xString.value->chars[pos] );
		break;
	}
	return NULL;
}

static int DaoString_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	if( N == 0 ){
		if( value->tid > DAO_FLOAT ) return DAO_ERROR_VALUE;
		return DAO_OK;
	}
	if( N != 1 ) return DAO_ERROR_INDEX;
	if( index[0]->tid == DAO_RANGE ){
		if( ! DaoType_CheckRangeIndex( index[0] ) ) return DAO_ERROR_INDEX;
		if( value->tid != DAO_STRING ) return DAO_ERROR_VALUE;
	}else{
		if( ! DaoType_CheckNumberIndex( index[0] ) ) return DAO_ERROR_INDEX;
		if( value->tid > DAO_FLOAT ) return DAO_ERROR_VALUE;
	}
	return DAO_OK;
}

static int DaoString_DoSetItem( DaoValue *self, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	daoint pos, end, size = self->xString.value->size;
	DIndexRange range;

	if( N == 0 ){
		if( value->type != DAO_STRING ) return DAO_ERROR_VALUE;
		DString_Assign( self->xString.value, value->xString.value );
	}

	if( N != 1 ) return DAO_ERROR_INDEX;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return DAO_ERROR_INDEX;
		if( value->type > DAO_FLOAT ) return DAO_ERROR_VALUE;
		self->xString.value->chars[pos] = DaoValue_GetInteger( value );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		pos = range.pos;
		end = range.end;
		DString_Insert( self->xString.value, value->xString.value, pos, end - pos, -1 );
		break;
	default: return DAO_ERROR_INDEX;
	}
	return DAO_OK;
}

static DaoType* DaoString_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code == DVM_SIZE ) return dao_type_int;
	return NULL;
}

static DaoValue* DaoString_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	if( op->code == DVM_SIZE ) DaoProcess_PutInteger( proc, self->xString.value->size );
	return NULL;
}

static DaoType* DaoString_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	switch( op->code ){
	case DVM_ADD :
	case DVM_DIV :
		if( left->tid == DAO_STRING && right->tid == DAO_STRING ) return dao_type_string;
		break;
	case DVM_LT : case DVM_LE :
	case DVM_EQ : case DVM_NE :
	case DVM_IN :
		if( left->tid == DAO_STRING && right->tid == DAO_STRING ) return dao_type_bool;
		break;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoString_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	DString *res;
	daoint pos;
	int D = 0;

	if( left->type != DAO_STRING || right->type != DAO_STRING ) return NULL;

	switch( op->code ){
	case DVM_ADD :
		res = DaoProcess_PutString( proc, left->xString.value );
		DString_Append( res, right->xString.value );
		break;
	case DVM_DIV :
		res = DaoProcess_PutString( proc, right->xString.value );
		DString_MakePath( left->xString.value, res );
		break;
	case DVM_LT :
	case DVM_LE :
		D = DString_CompareUTF8( left->xString.value, right->xString.value );
		DaoProcess_PutBoolean( proc, D < (op->code == DVM_LE) );
		break;
	case DVM_EQ :
	case DVM_NE :
		D = DString_Compare( left->xString.value, right->xString.value );
		DaoProcess_PutBoolean( proc, (op->code == DVM_EQ) ? D == 0 : D != 0 );
		break;
	case DVM_IN :
		pos = DString_Find( right->xString.value, left->xString.value, 0 );
		DaoProcess_PutBoolean( proc, pos != DAO_NULLPOS );
		break;
	}
	return NULL;
}

static int DaoString_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid == DAO_STRING && right->tid == DAO_STRING ) return DAO_OK;
	return DAO_ERROR;
}

static int DaoString_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	return DString_Compare( left->xString.value, right->xString.value );
}

static DaoType* DaoString_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_STRING ) return type;
	return NULL;
}

static DaoValue* DaoString_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	switch( type->tid ){
	case DAO_BOOLEAN :
		num->xBoolean.value = strcmp( self->xString.value->chars, "true" ) == 0;
		return num;
	case DAO_INTEGER :
		num->xInteger.value = DString_ToInteger( self->xString.value );
		return num;
	case DAO_FLOAT :
		num->xFloat.value = DString_ToFloat( self->xString.value );
		return num;
	case DAO_STRING :
		return self;
	default : break;
	}
	return NULL;
}

DaoType* DaoString_CheckForEach( DaoType *self, DaoRoutine *ctx )
{
	return dao_type_iterator_int;
}

void DaoString_DoForEach( DaoValue *self, DaoIterator *iterator, DaoProcess *proc )
{
	iterator->values[0]->xBoolean.value = self->xString.value->size > 0;
	iterator->values[0]->xInteger.value = 0;
}

static void DaoString_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoStream_WriteString( stream, self->xString.value );
}


static void DaoSTR_New( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutChars( proc, "" );
	daoint i, count = p[0]->xInteger.value;
	size_t width, ch = p[1]->xInteger.value;
	char buffer[8];

	DString_AppendWChar( res, ch );
	width = res->size;
	memcpy( buffer, res->chars, width );
	DString_Resize( res, count * width );
	if( width == 1 ){
		memset( res->chars, buffer[0], res->size );
	}else{
		for(i=0; i<count; ++i) memcpy( res->chars + i * width, buffer, width );
	}
}

static void DaoSTR_New2( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *index = (DaoValue*)(void*)&idint;
	DString *string = DaoProcess_PutChars( proc, "" );
	daoint i, entry, size = p[0]->xInteger.value;
	DaoVmCode *sect;

	if( size < 0 ){
		DaoProcess_RaiseError( proc, "Param", "Invalid parameter value" );
		return;
	}
	if( size == 0 ) return;
	sect = DaoProcess_InitCodeSection( proc, 1 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(i=0; i<size; i++){
		idint.value = i;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		DString_Append( string, proc->stackValues[0]->xString.value );
	}
	DaoProcess_PopFrame( proc );
}

static void DaoSTR_Size( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	int chars = p[1]->xBoolean.value;
	DaoProcess_PutInteger( proc, chars ? DString_GetCharCount( self ) : self->size );
}

static void DaoSTR_Insert( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *str = p[1]->xString.value;
	DString *res = DaoProcess_PutString( proc, self );
	daoint pos = p[2]->xInteger.value;
	if( pos < 0 ) pos += self->size;
	if( pos < 0 || pos > self->size ){
		DaoProcess_RaiseError( proc, "Index::Range", NULL );
		return;
	}
	DString_Insert( res, str, pos, 0, -1 );
}

static void DaoSTR_Erase( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *res = DaoProcess_PutString( proc, self );
	daoint pos = p[1]->xInteger.value;
	daoint count = p[2]->xInteger.value;
	if( pos < 0 ) pos += self->size;
	if( pos < 0 || pos > self->size ){
		DaoProcess_RaiseError( proc, "Index::Range", NULL );
		return;
	}
	DString_Erase( res, pos, count );
}

static void DaoSTR_Chop( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutString( proc, p[0]->xString.value );
	daoint utf8 = p[1]->xBoolean.value;
	DString_Chop( res, utf8 );
}

static void DaoSTR_Trim( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutString( proc, p[0]->xString.value );
	daoint head = p[1]->xEnum.value & 0x1;
	daoint tail = p[1]->xEnum.value & 0x2;
	daoint utf8 = p[2]->xBoolean.value;
	DString_Trim( res, head, tail, utf8 );
}

static void DaoSTR_Find( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *str = p[1]->xString.value;
	daoint from = p[2]->xInteger.value;
	daoint pos = DAO_NULLPOS;
	if( p[3]->xBoolean.value ){
		pos = DString_RFind( self, str, from );
	}else{
		pos = DString_Find( self, str, from );
	}
	DaoProcess_PutInteger( proc, pos );
}

static void DaoSTR_Contains( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	int i, res = 1;
	for(i=1; i<N; ++i){
		DaoTuple *tuple = (DaoTuple*) p[i];
		DString *keyword = tuple->values[1]->xString.value;
		daoint pos = 0, len = keyword->size;
		if( tuple->values[0]->type == DAO_INTEGER ){
			pos = tuple->values[0]->xInteger.value;
			if( pos < 0 ) pos += self->size - (len - 1);
		}else{ /* DAO_ENUM: */
			pos = tuple->values[0]->xEnum.value == 0 ? 0 : self->size - len;
		}
		if( pos < 0 || (self->size - pos) < len ){
			res = 0;
			break;
		}
		res = res && memcmp( self->chars + pos, keyword->chars, len ) == 0;
		if( res == 0 ) break;
	}
	DaoProcess_PutBoolean( proc, res );
}

static void DaoSTR_Replace( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutChars( proc, "" );
	DString *self = p[0]->xString.value;
	DString *str1 = p[1]->xString.value;
	DString *str2 = p[2]->xString.value;
	daoint index = p[3]->xInteger.value;
	daoint pos, offset = 0, count = 0;

	DString_Reserve( res, self->size + (str2->size - str1->size) );
	if( index == 0 ){
		pos = DString_Find( self, str1, offset );
		if( pos == DAO_NULLPOS ) pos = self->size;
		while( offset < self->size ){
			count += pos < self->size;
			DString_AppendBytes( res, self->chars + offset, pos - offset );
			if( pos < self->size ) DString_Append( res, str2 );
			offset = pos + str1->size;
			pos = DString_Find( self, str1, offset );
			if( pos == DAO_NULLPOS ) pos = self->size;
		}
	}else if( index > 0){
		pos = DString_Find( self, str1, offset );
		while( pos != DAO_NULLPOS ){
			count ++;
			offset = pos + str1->size;
			if( count == index ){
				DString_AppendBytes( res, self->chars, pos );
				DString_Append( res, str2 );
				DString_AppendBytes( res, self->chars + offset, self->size - offset  );
				break;
			}
			pos = DString_Find( self, str1, offset );
		}
		if( count != index ) DString_Assign( res, self );
	}else{
		offset = DAO_NULLPOS;
		pos = DString_RFind( self, str1, offset );
		while( pos != DAO_NULLPOS ){
			count --;
			offset = pos - str1->size;
			if( count == index ){
				DString_AppendBytes( res, self->chars, offset + 1 );
				DString_Append( res, str2 );
				DString_AppendBytes( res, self->chars + pos + 1, self->size - pos - 1 );
				break;
			}
			pos = DString_RFind( self, str1, offset );
		}
		if( count != index ) DString_Assign( res, self );
	}
}

static void DaoSTR_Expand( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoString *key = NULL;
	DString *self = p[0]->xString.value;
	DString *spec = p[2]->xString.value;
	DString *res = NULL, *sub = NULL;
	DaoTuple *tup = DaoValue_CastTuple( p[1] );
	DaoValue *val = NULL;
	DMap  *keys = NULL;
	DNode *node = NULL;
	daoint keep = p[3]->xBoolean.value;
	daoint i, pos1, pos2, prev = 0;
	char spec2;
	int replace;
	int ch;

	if( DString_Size( spec ) ==0 ){
		DaoProcess_PutString( proc, self );
		return;
	}
	if( tup ){
		keys = tup->ctype->mapNames;
	}else{
		keys = p[1]->xMap.value;
	}

	res = DaoProcess_PutChars( proc, "" );
	key = DaoString_New();
	sub = DString_New();
	spec2 = spec->chars[0];
	pos1 = DString_FindChar( self, spec2, prev );
	while( pos1 != DAO_NULLPOS ){
		pos2 = DString_FindChar( self, ')', pos1 );
		replace = 0;
		if( pos2 != DAO_NULLPOS && self->chars[pos1+1] == '(' ){
			replace = 1;
			for(i=pos1+2; i<pos2; i++){
				ch = self->chars[i];
				if( ch != '-' && ch != '_' && ch != ' ' && ! isalnum( ch ) ){
					replace = 0;
					break;
				}
			}
			if( replace ){
				DString_SubString( self, key->value, pos1+2, pos2-pos1-2 );
				node = DMap_Find( keys, tup ? (void*) key->value : (void*) key );
				if( node ){
					if( tup ){
						val = tup->values[node->value.pInt];
					}else{
						val = node->value.pValue;
					}
				}else if( keep ){
					replace = 0;
				}else{
					DString_Clear( key->value );
					val = (DaoValue*) key;
				}
			}
		}
		DString_SubString( self, sub, prev, pos1 - prev );
		DString_Append( res, sub );
		prev = pos1 + 1;
		if( replace ){
			DString *s = DaoValue_GetString( val, sub );
			DString_Append( res, s );
			prev = pos2 + 1;
		}else{
			DString_AppendChar( res, spec2 );
		}
		pos1 = DString_FindChar( self, spec2, prev );
	}
	DString_SubString( self, sub, prev, DString_Size( self ) - prev );
	DString_Append( res, sub );
	DString_Delete( sub );
	DaoString_Delete( key );
}

static void DaoSTR_Split( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *list = DaoProcess_PutList( proc );
	DaoValue *value = (DaoValue*) DaoString_New();
	DString *self = p[0]->xString.value;
	DString *delm = p[1]->xString.value;
	DString *str = value->xString.value;
	daoint dlen = DString_Size( delm );
	daoint size = DString_Size( self );
	daoint last = 0;
	daoint posDelm = DString_Find( self, delm, last );

	if( N ==1 || DString_Size( delm ) ==0 ){
		uchar_t *bytes = (unsigned char*) self->chars;
		daoint i = 0;
		while( i < size ){
			daoint pos = DString_LocateChar( self, i, 0 );
			int w = pos == DAO_NULLPOS ? 1 : DString_UTF8CharSize( bytes[i] );
			DString_SetBytes( str, (char*) bytes + i, w );
			DList_Append( list->value, value );
			i += w;
		}
		DaoString_Delete( (DaoString*) value );
		return;
	}
	while( posDelm != DAO_NULLPOS ){
		DString_SubString( self, str, last, posDelm-last );
		DList_Append( list->value, value );

		last = posDelm + dlen;
		posDelm = DString_Find( self, delm, last );
	}
	DString_SubString( self, str, last, size-last );
	DList_Append( list->value, value );
	DaoString_Delete( (DaoString*) value );
}

static void DaoSTR_Fetch( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	daoint group = p[2]->xInteger.value;
	daoint start = p[3]->xInteger.value;
	daoint end = p[4]->xInteger.value;
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	int matched = 0;

	DString_Clear( pt ); /* passed in by value; */
	if( start < 0 ) start += self->size;
	if( end < 0 ) end += self->size;
	if( end == 0 ) end = DString_Size( self ) - 1;
	if( (patt == NULL) | (start < 0) | (end < 0) ) goto Done;
	if( DaoRegex_Match( patt, self, & start, & end ) ){
		matched = 1;
		if( group > 0 && DaoRegex_SubMatch( patt, group, & start, & end ) ==0 ) matched = 0;
	}
Done:
	if( matched ) DString_SubString( self, pt, start, end-start+1 );
	DaoProcess_PutString( proc, pt );
}

static void DaoSTR_Match( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	daoint group = p[2]->xInteger.value;
	daoint start = p[3]->xInteger.value;
	daoint end = p[4]->xInteger.value;
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	int matched = 0;

	if( start < 0 ) start += self->size;
	if( end < 0 ) end += self->size;
	if( end == 0 ) end = DString_Size( self ) - 1;
	if( (patt == NULL) | (start < 0) | (end < 0) ) goto Done;
	if( DaoRegex_Match( patt, self, & start, & end ) ){
		matched = 1;
		if( group > 0 && DaoRegex_SubMatch( patt, group, & start, & end ) ==0 ) matched = 0;
	}
Done:
	if( matched ){
		DaoTuple *tuple = DaoProcess_PutTuple( proc, 2 );
		tuple->values[0]->xInteger.value = start;
		tuple->values[1]->xInteger.value = end;
	}else{
		DaoProcess_PutNone( proc );
	}
}

static void DaoSTR_Change( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutChars( proc, "" );
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	DString *str = p[2]->xString.value;
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	daoint start = p[4]->xInteger.value;
	daoint end = p[5]->xInteger.value;
	daoint index = p[3]->xInteger.value;
	if( start < 0 ) start += self->size;
	if( end < 0 ) end += self->size;
	if( end == 0 ) end = DString_Size( self ) - 1;
	if( (patt == NULL) | (start < 0) | (end < 0) ) return;
	DaoRegex_ChangeExt( patt, self, res, str, index, & start, & end );
}

static void DaoSTR_Capture( DaoProcess *proc, DaoValue *p[], int N )
{
	int gid;
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	daoint start = p[2]->xInteger.value;
	daoint end = p[3]->xInteger.value;
	DaoList *list = DaoProcess_PutList( proc );
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	DaoString *subs;

	if( start < 0 ) start += self->size;
	if( end < 0 ) end += self->size;
	if( end == 0 ) end = DString_Size( self ) - 1;
	if( (patt == NULL) | (start < 0) | (end < 0) ) return;
	if( DaoRegex_Match( patt, self, & start, & end ) ==0 ) return;
	subs = DaoString_New();
	for(gid=0; gid<=patt->group; ++gid){
		DString_Clear( subs->value );
		if( DaoRegex_SubMatch( patt, gid, & start, & end ) ){
			DString_SubString( self, subs->value, start, end-start+1 );
		}
		DList_Append( list->value, (DaoValue*) subs );
	}
	DaoString_Delete( subs );
}

static void DaoSTR_Extract( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	int type = p[2]->xEnum.value;
	daoint offset, start, end, size, matched, done = 0;
	DaoList *list = DaoProcess_PutList( proc );
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	DaoString *subs;
	
	size = DString_Size( self );
	start = offset = 0;
	end = size - 1;
	if( size == 0 || patt == NULL ) return;
	subs = DaoString_New();
	while( (matched = DaoRegex_Match( patt, self, & start, & end )) || done == 0 ){
		if( matched == 0 ) start = size;
		if( type == 0 || type == 2 ){
			if( start > offset ){
				DString_SubString( self, subs->value, offset, start-offset );
				DaoList_Append( list, (DaoValue*) subs );
			}
		}
		if( matched == 0 && done != 0 ) break;
		if( type == 0 || type == 1 ){
			if( matched ){
				DString_SubString( self, subs->value, start, end-start+1 );
				DaoList_Append( list, (DaoValue*) subs );
			}
		}
		done = matched == 0;
		start = offset = end + 1;
		end = size - 1;
	}
	DaoString_Delete( subs );
}

static void DaoSTR_Scan( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	DString *pt = p[1]->xString.value;
	daoint from = p[2]->xInteger.value;
	daoint to = p[3]->xInteger.value;
	daoint entry, offset, start, end, matched, done = 0;
	DaoList *list = DaoProcess_PutList( proc );
	DaoRegex *patt = DaoProcess_MakeRegex( proc, pt );
	DaoInteger startpos = {DAO_INTEGER,0,0,0,0,0};
	DaoInteger endpos = {DAO_INTEGER,0,0,0,0,0};
	DaoEnum denum = {DAO_ENUM,DAO_ENUM_SYM,0,0,0,0,0,NULL};
	DaoValue *res;
	DaoVmCode *sect;

	if( from < 0 ) from += self->size;
	if( to < 0 ) to += self->size;
	if( to == 0 ) to = DString_Size( self ) - 1;
	if( (patt == NULL) | (from < 0) | (to < 0) ){
		DaoProcess_RaiseError( proc, "Param", NULL );
		return;
	}

	sect = DaoProcess_InitCodeSection( proc, 3 );
	if( sect == NULL ) return;

	denum.etype = DaoNamespace_MakeEnumType( proc->activeNamespace, "unmatched,matched" );
	denum.subtype = DAO_ENUM_STATE;
	entry = proc->topFrame->entry;

	start = offset = from;
	end = to;
	while( (matched = DaoRegex_Match( patt, self, & start, & end )) || done == 0 ){
		if( matched == 0 ) start = to + 1;
		if( start > offset ){
			startpos.value = offset;
			endpos.value = start-1;
			denum.value = 0;
			if( sect->b > 0 ) DaoProcess_SetValue( proc, sect->a, (DaoValue*) & startpos );
			if( sect->b > 1 ) DaoProcess_SetValue( proc, sect->a+1, (DaoValue*) & endpos );
			if( sect->b > 2 ) DaoProcess_SetValue( proc, sect->a+2, (DaoValue*) & denum );
			proc->topFrame->entry = entry;
			DaoProcess_Execute( proc );
			if( proc->status == DAO_PROCESS_ABORTED ) break;
			res = proc->stackValues[0];
			if( res && res->type != DAO_NONE ) DaoList_Append( list, res );
		}
		if( matched == 0 && done != 0 ) break;
		if( matched ){
			startpos.value = start;
			endpos.value = end;
			denum.value = 1;
			if( sect->b > 0 ) DaoProcess_SetValue( proc, sect->a, (DaoValue*) & startpos );
			if( sect->b > 1 ) DaoProcess_SetValue( proc, sect->a+1, (DaoValue*) & endpos );
			if( sect->b > 2 ) DaoProcess_SetValue( proc, sect->a+2, (DaoValue*) & denum );
			proc->topFrame->entry = entry;
			DaoProcess_Execute( proc );
			if( proc->status == DAO_PROCESS_ABORTED ) break;
			res = proc->stackValues[0];
			if( res && res->type != DAO_NONE ) DaoList_Append( list, res );
		}
		done = matched == 0;
		start = offset = end + 1;
		end = to;
	}
	DaoProcess_PopFrame( proc );
}

static void DaoSTR_Convert( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *res = DaoProcess_PutString( proc, p[0]->xString.value );
	int bl = 1;
	switch( p[1]->xEnum.value ){
	case 0 : bl = DString_ToLocal( res ); break; /* local */
	case 1 : bl = DString_ToUTF8( res ); break; /* utf8 */
	case 2 : DString_ToLower( res ); break; /* lower */
	case 3 : DString_ToUpper( res ); break; /* upper */
	}
	if( bl == 0 ) DaoProcess_RaiseError( proc, "Value", "Conversion failed" );
}

static void DaoSTR_Functional( DaoProcess *proc, DaoValue *p[], int np, int funct )
{
	DString *string = NULL;
	DaoString *self = & p[0]->xString;
	DaoInteger chint = {DAO_INTEGER,0,0,0,0,0};
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *res, *index = (DaoValue*)(void*)&idint;
	DaoValue *chr = (DaoValue*)(void*)&chint;
	DaoVmCode *sect = NULL;
	DString *data = self->value;
	daoint unit = p[1]->xEnum.value;
	daoint entry, i, N = data->size;
	char *chars = data->chars, *end = chars + N;
	DCharState state = { 1, 1, 0 };

	switch( funct ){
	case DVM_FUNCT_COLLECT :
		string = DaoProcess_PutChars( proc, "" );
		DString_Reserve( string, self->value->size );
		break;
	}
	sect = DaoProcess_InitCodeSection( proc, 2 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(i=0; i<N; ){
		if( unit ){
			state = DString_DecodeChar( chars, end );
		}else{
			state.value = data->chars[i];
		}
		chars += state.width;
		i += state.width;
		idint.value = i;
		chint.value = state.value;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, chr );
		if( sect->b >1 ) DaoProcess_SetValue( proc, sect->a+1, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		switch( funct ){
		case DVM_FUNCT_COLLECT :
			if( res->type == DAO_NONE ) break;
			if( unit ){
				DString_AppendWChar( string, res->xInteger.value );
			}else{
				DString_AppendChar( string, res->xInteger.value );
			}
			break;
		}
	}
	DaoProcess_PopFrame( proc );
}

static void DaoSTR_Iterate( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoSTR_Functional( proc, p, N, DVM_FUNCT_ITERATE );
}

static void DaoSTR_Index( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	daoint index = p[1]->xInteger.value;
	daoint pos = DString_GetByteIndex( self, index );
	DaoProcess_PutInteger( proc, pos );
}

static void DaoSTR_Char( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *self = p[0]->xString.value;
	daoint index = p[1]->xInteger.value;
	daoint pos = DString_GetByteIndex( self, index );
	DCharState state = DString_DecodeChar( self->chars + pos, self->chars + self->size );
	DaoProcess_PutBytes( proc, self->chars + pos, state.width );
}


static DaoFunctionEntry daoStringMeths[] =
{
	{ DaoSTR_New,
		"string( count: int, char = 0 ) => string"
		/*
		// Create and return a string that is composed of "count" of "char".
		*/
	},
	{ DaoSTR_New2,
		"string( count: int )[index: int => string] => string"
		/*
		// Create and return a string that is concatenation of the resulting
		// strings from the exection of the code section.
		*/
	},
	{ DaoSTR_Size,
		"size( invar self: string, utf8 = false ) => int"
		/*
		// Return the number of bytes or characters in the string.
		*/
	},
	{ DaoSTR_Insert,
		"insert( invar self: string, str: string, pos = 0 ) => string"
		/*
		// Insert "str" at "pos";
		// Return a new string;
		*/
	},
	{ DaoSTR_Erase,
		"erase( invar self: string, pos = 0, count = -1 ) => string"
		/*
		// Erase "count" bytes starting from "pos";
		// Return a new string;
		*/
	},
	{ DaoSTR_Chop,
		"chop( invar self: string, utf8 = false ) => string"
		/*
		// Chop EOF, '\n' and/or '\r' off the end of the string;
		// -- EOF  is first checked and removed if found;
		// -- '\n' is then checked and removed if found;
		// -- '\r' is last checked and removed if found;
		// If "utf8" is not zero, all bytes that do not constitute a
		// valid UTF-8 encoding sequence are removed from the end.
		*/
	},
	{ DaoSTR_Trim,
		"trim( invar self: string, where: enum<head;tail> = $head+$tail, utf8 = false )"
			"=> string"
		/*
		// Trim whitespaces from the head and/or the tail of the string;
		// If "utf8" is not zero, all bytes that do not constitute a
		// valid UTF-8 encoding sequence are trimmed as well.
		*/
	},
	{ DaoSTR_Find,
		"find( invar self: string, str: string, from = 0, reverse = false ) => int"
		/*
		// Find the first occurrence of "str" in this string, searching from "from";
		// If "reverse" is zero, search forward, otherwise backward;
		// Return -1, if "str" is not found; Otherwise,
		// Return the index of the first byte of the found substring for forward searching;
		// Return the index of the last byte of the found substring for backward searching;
		*/
	},
	{ DaoSTR_Contains,
		"contains( invar self: string, "
			"...: tuple<pos:int|enum<prefix,suffix>,keyword:string> ) => bool"
		/*
		// Check if the string contain specific keywords at specific locations.
		// Locations can be specified by byte indices or symbol $prefix or $suffix.
		*/
	},
	{ DaoSTR_Convert,
		"convert( invar self: string, to: enum<local,utf8,lower,upper> ) => string"
		/*
		// Convert the string:
		// -- To local encoding if the string is encoded in UTF-8;
		// -- To UTF-8 encoding if the string is not encoded in UTF-8;
		// -- To lower cases;
		// -- To upper cases;
		*/
	},
	{ DaoSTR_Replace,
		"replace( invar self: string, str1: string, str2: string, index = 0 ) => string"
		/*
		// Replace the substring "str1" in "self" to "str2";
		// Replace all occurrences of "str1" to "str2" if "index" is zero;
		// Otherwise, replace only the "index"-th occurrence;
		// Positive "index" is counted forwardly;
		// Negative "index" is counted backwardly;
		*/
	},
	{ DaoSTR_Expand,
		"expand( invar self: string, invar subs: map<string,string>"
			"|tuple<...:int|float|string>, spec = \"$\", keep = true ) => string"
		/*
		// Expand this string into a new string with substrings from the keys
		// of "subs" substituted with the corresponding values of "subs".
		// If "spec" is not an empty string, each key has to be occurred inside
		// a pair of parenthesis preceded with "spec", and the "spec", the
		// parenthesis and the key are together substituted by the corresponding
		// value from "subs"; If "spec" is not empty and "keep" is zero, "spec(key)"
		// that contain substrings not found in the keys of "subs" are removed;
		// Otherwise kept.
		*/
	},
	{ DaoSTR_Split,
		"split( invar self: string, sep = \"\" ) => list<string>"
		/*
		// Split the string by seperator "sep", and return the tokens as a list.
		// If "sep" is empty, split at character boundaries assuming UTF-8 encoding.
		*/
	},
	{ DaoSTR_Fetch,
		"fetch( invar self: string, pattern: string, group = 0, start = 0, end = -1 )"
			"=> string"
		/*
		// Fetch the substring that matches the "group"-th group of pattern "pattern".
		// Only the region between "start" and "end" is searched.
		// Negative index starts from the last byte of the string.
		*/
	},
	{ DaoSTR_Match,
		"match( invar self: string, pattern: string, group = 0, start = 0, end = -1 )"
			"=> tuple<start:int,end:int>|none"
		/*
		// Match part of this string to pattern "pattern".
		// If matched, the indexes of the first and the last byte of the matched
		// substring will be returned as a tuple. If not matched, "none" is returned.
		// Parameter "start" and "end" have the same meaning as in string::fetch().
		*/
	},
	{ DaoSTR_Change,
		"change( invar self: string, pattern: string, target: string, index = 0, "
			"start = 0, end = -1 ) => string"
		/*
		// Change the part(s) of the string that match pattern "pattern" to "target".
		// The target string "target" can contain back references from pattern "pattern".
		// If "index" is zero, all matched parts are changed; otherwise, only
		// the "index" match is changed.
		// Parameter "start" and "end" have the same meaning as in string::fetch().
		// Returns a shallow copy of the self string.
		*/
	},
	{ DaoSTR_Capture,
		"capture( invar self: string, pattern: string, start = 0, end = -1 ) => list<string>"
		/*
		// Match pattern "pattern" to the string, and capture all the substrings that
		// match to each of the groups of "pattern". Note that the pattern groups are
		// indexed starting from one, and zero index is reserved for the whole pattern.
		// The strings in the returned list correspond to the groups that have the
		// same index as that of the strings in the list.
		// Parameter "start" and "end" have the same meaning as in string::fetch().
		*/
	},
	{ DaoSTR_Extract,
		"extract( invar self: string, pattern: string, "
			"mtype: enum<both,matched,unmatched> = $matched ) => list<string>"
		/*
		// Extract the substrings that match to, or are between the matched ones,
		// or both, and return them as a list.
		*/
	},
	{ DaoSTR_Scan,
		"scan( invar self: string, pattern: string, start = 0, end = -1 )"
			"[start: int, end: int, state: enum<unmatched,matched> => none|@V]"
			"=> list<@V>"
		/*
		// Scan the string with pattern "pattern", and invoke the attached code
		// section for each matched substring and substrings between matches.
		// The start and end index as well as the state of matching or not matching
		// can be passed to the code section.
		// Parameter "start" and "end" have the same meaning as in string::fetch().
		// 
		// Use "none|@V" for the code section return, so that if "return none" is used first,
		// it will not be specialized to "none|none", which is the case for "@V|none".
		*/
	},

	{ DaoSTR_Iterate,
		"iterate( invar self: string, unit: enum<byte,char> = $byte )[char: int, index: int]"
		/*
		// Iterate over each unit of the string.
		// If "unit" is "$byte", iterate per byte;
		// If "unit" is "$char", iterate per character; Assuming UTF-8 encoding;
		// Each byte that is not part of a valid UTF-8 encoding unit is iterated once.
		// For the code section parameters, the first will hold the byte value or
		// character codepoint for each iteration, and the second will be the byte
		// location in the string.
		*/
	},

	{ DaoSTR_Index,
		"offset( invar self: string, charIndex: int ) => int"
		/*
		// Get byte offset for the character with index "charIndex";
		*/
	},
	{ DaoSTR_Char,
		"char( invar self: string, charIndex: int ) => string"
		/*
		// Get the character with index "charIndex";
		*/
	},
	{ NULL, NULL }
};


DaoTypeCore daoStringCore =
{
	"string",                                            /* name */
	{ NULL },                                            /* bases */
	NULL,                                                /* numbers */
	daoStringMeths,                                      /* methods */
	DaoValue_CheckGetField,     DaoValue_DoGetField,     /* GetField */
	NULL,                       NULL,                    /* SetField */
	DaoString_CheckGetItem,     DaoString_DoGetItem,     /* GetItem */
	DaoString_CheckSetItem,     DaoString_DoSetItem,     /* SetItem */
	DaoString_CheckUnary,       DaoString_DoUnary,       /* Unary */
	DaoString_CheckBinary,      DaoString_DoBinary,      /* Binary */
	DaoString_CheckComparison,  DaoString_DoComparison,  /* Comparison */
	DaoString_CheckConversion,  DaoString_DoConversion,  /* Conversion */
	DaoString_CheckForEach,     DaoString_DoForEach,     /* ForEach */
	DaoString_Print,                                     /* Print */
	NULL,                                                /* Slice */
	NULL,                                                /* Copy */
	DaoString_Delete,                                    /* Delete */
	NULL                                                 /* HandleGC */
};









DaoEnum* DaoEnum_New( DaoType *type, int value )
{
	DaoEnum *self = (DaoEnum*) dao_malloc( sizeof(DaoEnum) );
	DaoValue_Init( self, DAO_ENUM );
	self->subtype = type ? type->subtid : DAO_ENUM_SYM;
	self->value = value;
	self->etype = type;
	if( type ) GC_IncRC( type );
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

DaoEnum* DaoEnum_Copy( DaoEnum *self, DaoType *type )
{
	DaoEnum *copy = DaoEnum_New( self->etype, self->value );
	copy->subtype = self->subtype;
	if( self->etype != type && type ){
		DaoEnum_SetType( copy, type );
		DaoEnum_SetValue( copy, self );
	}
	return copy;
}

void DaoEnum_Delete( DaoEnum *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	if( self->etype ) GC_DecRC( self->etype );
	dao_free( self );
}

void DaoEnum_MakeName( DaoEnum *self, DString *name )
{
	DMap *mapNames;
	DNode *node;
	DString_Clear( name );
	mapNames = self->etype->mapNames;
	for(node=DMap_First(mapNames); node; node=DMap_Next(mapNames,node)){
		if( self->subtype == DAO_ENUM_FLAG ){
			if( (node->value.pInt & self->value) != node->value.pInt ) continue;
		}else if( node->value.pInt != self->value ){
			continue;
		}
		DString_AppendChar( name, '$' );
		DString_Append( name, node->key.pString );
	}
}

void DaoEnum_SetType( DaoEnum *self, DaoType *type )
{
	type = DaoType_GetBaseType( type );
	if( self->etype == type ) return;
	GC_Assign( & self->etype, type );
	self->subtype = type->subtid;
	self->value = type->mapNames->root->value.pInt;
}

int DaoEnum_SetSymbols( DaoEnum *self, const char *symbols )
{
	DString *names;
	daoint first = 0;
	daoint value = 0;
	int notfound = 0;
	int i, n, k = 0;

	names = DString_New();
	DString_SetChars( names, symbols );
	for(i=0,n=names->size; i<n; i++){
		char ch = names->chars[i];
		if( ch == '$' || ch == ',' || ch == ';' ) names->chars[i] = 0;
	}
	i = 0;
	if( names->chars[0] == '\0' ) i += 1;
	do{ /* for multiple symbols */
		DString name = DString_WrapChars( names->chars + i );
		DNode *node = DMap_Find( self->etype->mapNames, &name );
		if( node ){
			if( ! k ) first = node->value.pInt;
			value |= node->value.pInt;
			k += 1;
		}else{
			notfound = 1;
		}
		i += name.size + 1;
	} while( i < names->size );
	DString_Delete( names );
	if( k == 0 ) return 0;
	if( self->subtype == DAO_ENUM_STATE && k > 1 ){
		self->value = first;
		return 0;
	}
	self->value = value;
	return notfound == 0;
}

int DaoEnum_SetValue( DaoEnum *self, DaoEnum *other )
{
	DMap *selfNames = self->etype->mapNames;
	DMap *otherNames = other->etype->mapNames;
	DNode *node, *search;
	int ret = 0;

	if( self->etype == other->etype ){
		self->value = other->value;
		return 1;
	}
	if( self->subtype == DAO_ENUM_SYM ) return 0;

	self->value = 0;
	if( other->subtype == DAO_ENUM_STATE || other->subtype == DAO_ENUM_SYM ){
		for(node=DMap_First(otherNames); node; node=DMap_Next(otherNames,node)){
			if( node->value.pInt != other->value ) continue;
			search = DMap_Find( selfNames, node->key.pVoid );
			if( search == NULL ) return 0;
			self->value |= search->value.pInt;
			ret += 1;
		}
		/* State or bool enums are supposed to have only one symbol; */
		ret = ret == 1;
	}else{
		for(node=DMap_First(otherNames); node; node=DMap_Next(otherNames,node)){
			if( (node->value.pInt & other->value) != node->value.pInt ) continue;
			search = DMap_Find( selfNames, node->key.pVoid );
			if( search == NULL ) return 0;
			self->value |= search->value.pInt;
			ret += 1;
		}
		if( self->subtype == DAO_ENUM_STATE ) ret = ret==1;
	}
	return ret;
}

int DaoEnum_AddValue( DaoEnum *self, DaoEnum *other )
{
	DMap *selfNames = self->etype->mapNames;
	DMap *otherNames = other->etype->mapNames;
	DNode *node, *search;

	if( self->subtype != DAO_ENUM_FLAG ) return 0;

	if( self->etype == other->etype ){
		self->value |= other->value;
		return 1;
	}

	for(node=DMap_First(otherNames); node; node=DMap_Next(otherNames,node)){
		if( other->subtype == DAO_ENUM_FLAG ){
			if( (node->value.pInt & other->value) != node->value.pInt ) continue;
		}else{
			if( node->value.pInt != other->value ) continue;
		}
		search = DMap_Find( selfNames, node->key.pVoid );
		if( search == NULL ) return 0;
		self->value |= search->value.pInt;
	}
	return other->subtype == DAO_ENUM_SYM;
}

int DaoEnum_RemoveValue( DaoEnum *self, DaoEnum *other )
{
	DMap *selfNames = self->etype->mapNames;
	DMap *otherNames = other->etype->mapNames;
	DNode *node, *search;

	if( self->subtype != DAO_ENUM_FLAG ) return 0;

	if( self->etype == other->etype ){
		self->value &= ~ other->value;
		return 1;
	}

	for(node=DMap_First(otherNames); node; node=DMap_Next(otherNames,node)){
		if( other->subtype == DAO_ENUM_FLAG ){
			if( (node->value.pInt & other->value) != node->value.pInt ) continue;
		}else{
			if( node->value.pInt != other->value ) continue;
		}
		search = DMap_Find( selfNames, node->key.pVoid );
		if( search == NULL ) return 0;
		self->value &= ~search->value.pInt;
	}
	return other->subtype == DAO_ENUM_SYM;
}

int DaoEnum_Compare( DaoEnum *A, DaoEnum *B )
{
	DaoEnum E;
	if( A->etype == B->etype ){
		return A->value == B->value ? 0 : (A->value < B->value ? -1 : 1);
	}else if( A->subtype == DAO_ENUM_SYM && B->subtype == DAO_ENUM_SYM ){
		return DString_CompareUTF8( A->etype->name, B->etype->name );
	}else if( A->subtype == DAO_ENUM_SYM ){
		E = *B;
		if( DaoEnum_SetValue( & E, A ) == 0 ) goto CompareName;
		return E.value == B->value ? 0 : (E.value < B->value ? -1 : 1);
	}else if( B->subtype == DAO_ENUM_SYM ){
		E = *A;
		if( DaoEnum_SetValue( & E, B ) == 0 ) goto CompareName;
		return A->value == E.value ? 0 : (A->value < E.value ? -1 : 1);
	}else if( DString_EQ( A->etype->fname, B->etype->fname ) ){
		return A->value == B->value ? 0 : (A->value < B->value ? -1 : 1);
	}
CompareName:
	return DString_CompareUTF8( A->etype->fname, B->etype->fname );
}

static int DaoEnum_IsIn( DaoEnum *A, DaoEnum *B )
{
	DaoType *ta = A->etype;
	DaoType *tb = B->etype;
	int C = 0;

	if( ta == tb ){
		C = A->value == (A->value & B->value);
	}else{
		DMap *ma = ta->mapNames;
		DMap *mb = tb->mapNames;
		DNode *it, *node;
		C = 1;
		for(it=DMap_First(ma); it; it=DMap_Next(ma,it) ){
			if( A->subtype == DAO_ENUM_FLAG ){
				if( (it->value.pInt & A->value) != it->value.pInt ) continue;
			}else if( it->value.pInt != A->value ){
				continue;
			}
			if( (node = DMap_Find( mb, it->key.pVoid )) == NULL ){
				C = 0;
				break;
			}
			if( (node->value.pInt & B->value) != node->value.pInt ){
				C = 0;
				break;
			}
		}
	}
	return C;
}


static DaoType* DaoEnum_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code != DVM_TILDE ) return NULL;
	return type;
}

static DaoValue* DaoEnum_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	int min = 0, max = 0, envalue = 0; 
	DaoEnum *C, *A = (DaoEnum*) value;
	DaoType *TA;
	DNode *it;

	if( op->code != DVM_TILDE | value->type != DAO_ENUM ) return NULL;

	TA = A->etype;
	C = DaoProcess_PutValue( proc, A ); 

	it = DMap_First( TA->mapNames );
	if( it ) min = max = it->value.pInt;

	for(; it; it=DMap_Next(TA->mapNames,it)){
		if( it->value.pInt < min ) min = it->value.pInt;
		if( it->value.pInt > max ) max = it->value.pInt;
		envalue |= it->value.pInt;
	}    
	if( A->subtype == DAO_ENUM_FLAG ){
		C->value = envalue & (~A->value);
	}else if( A->value == min ){
		C->value = max; 
	}else{
		C->value = min; 
	}    
	return NULL;
}

static DaoType* DaoEnum_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	if( left->tid != DAO_ENUM || right->tid != DAO_ENUM ) return NULL;
	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
		if( left->subtid == DAO_ENUM_SYM && right->subtid == DAO_ENUM_SYM ){
			DaoType *type;
			DString *name = DString_New();
			DString_Assign( name, left->name );
			DString_Change( name, "enum%< (.*) %>", "%1", 0 ); 
			DString_Append( name, right->name );
			type = DaoNamespace_MakeEnumType( ns, name->chars );
			DString_Delete( name );
			return type;
		}else if( left->subtid == DAO_ENUM_FLAG ){
			return left;
		}    
		break;
	case DVM_BITAND : 
	case DVM_BITOR  : 
		if( left != right ) return NULL;
		return left;
	case DVM_AND: case DVM_OR :
	case DVM_LT : case DVM_LE :
	case DVM_EQ : case DVM_NE :
	case DVM_IN :
		return dao_type_bool;
	default: break;
	}
	return NULL;
}

static DaoValue* DaoEnum_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	DaoEnum *C;
	DaoEnum *A = (DaoEnum*) left;
	DaoEnum *B = (DaoEnum*) right;
	DaoType *TA = A->etype;
	DaoType *TB = B->etype;
	int D = 0;

	if( left->type != DAO_ENUM || right->type != DAO_ENUM ) return NULL;
	switch( op->code ){
	case DVM_ADD :
		C = DaoProcess_PutEnum( proc, "" );
		if( A->subtype == DAO_ENUM_SYM && B->subtype == DAO_ENUM_SYM ){
			DaoNamespace *NS = proc->activeNamespace;
			if( C->etype == NULL ){ /* Can happen in constant evaluation: */
				DaoType *tp; 
				DString_Reset( proc->string, 0 ); 
				DString_Append( proc->string, TA->mapNames->root->key.pString );
				DString_AppendChar( proc->string, ';' );
				DString_Append( proc->string, TB->mapNames->root->key.pString );
				tp = DaoNamespace_MakeEnumType( NS, proc->string->chars );
				DaoEnum_SetType( C, tp );
			}    
			DaoEnum_AddValue( C, A ); 
			DaoEnum_AddValue( C, B ); 
		}else{
			if( C != A ){
				if( C->etype == NULL ) DaoEnum_SetType( C, A->etype );
				DaoEnum_SetValue( C, A );
			}
			DaoEnum_AddValue( C, B );
		}
		return NULL;
	case DVM_SUB :
		C = DaoProcess_PutEnum( proc, "" );
		if( C != A ){
			if( C->etype == NULL ) DaoEnum_SetType( C, A->etype );
			DaoEnum_SetValue( C, A );
		}
		DaoEnum_RemoveValue( C, B );
		return NULL;
	case DVM_BITAND : 
	case DVM_BITOR  : 
		if( TA != BA ) return NULL;
		if( TA->subtype <= DAO_ENUM_STATE ) return NULL;
		C = DaoProcess_PutEnum( proc, "" );
		if( C->etype != A->etype ) DaoEnum_SetType( C, A->etype );
		switch( op->code ){
		case DVM_BITAND : C->value = A->value & B->value; break;
		case DVM_BITOR  : C->value = A->value | B->value; break;
		}
		return NULL;
	case DVM_AND:  D = A->value && B->value; break;
	case DVM_OR :  D = A->value || A->value; break;
	case DVM_LT :  D = DaoEnum_Compare( A, B ) < 0; break;
	case DVM_LE :  D = DaoEnum_Compare( A, B ) <= 0; break;
	case DVM_EQ :  D = DaoEnum_Compare( A, B ) == 0; break;
	case DVM_NE :  D = DaoEnum_Compare( A, B ) != 0; break;
	case DVM_IN :  D = DaoEnum_IsIn( A, B ); break;
	default : return NULL;
	}
	DaoProcess_PutBoolean( proc, D );
	return NULL;
}

static int DaoEnum_CheckComparison( DaoType *left, DaoType *right, DaoRoutine *ctx )
{
	if( left->tid == DAO_ENUM && right->tid == DAO_ENUM ) return DAO_OK;
	return DAO_ERROR;
}

static int DaoEnum_DoComparison( DaoValue *left, DaoValue *right, DaoProcess *proc )
{
	if( left->type != DAO_ENUM || right->type != DAO_ENUM ){
		// TODO:
		return left < right ? -1 : 1;
	}

	return DaoEnum_Compare( (DaoEnum*) left, (DaoEnum*) right );
}

static DaoType* DaoEnum_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid <= DAO_ENUM ) return type;
	return NULL;
}

static DaoValue* DaoEnum_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	int val = self->xEnum.value;
	switch( type->tid ){
	case DAO_BOOLEAN : num->xBoolean.value = val != 0; return num;
	case DAO_INTEGER : num->xInteger.value = val; return num;
	case DAO_FLOAT   : num->xFloat.value   = val; return num;
	case DAO_COMPLEX : num->xComplex.value.real = val; num->xComplex.value.imag = 0.0; return num;
	break;
	}
	if( type->tid == DAO_STRING ){
		DString *res = DaoProcess_PutChars( proc, "" );
		DaoEnum_MakeName( (DaoEnum*) self, res );
		return NULL;
	}
	return NULL;
}

static void DaoEnum_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	name = DString_New();
	DaoEnum_MakeName( (DaoEnum*) self, name );
	DaoStream_WriteChars( stream, name->chars );
	DaoStream_WriteChars( stream, "(" );
	DaoStream_WriteInt( stream, self->xEnum.value );
	DaoStream_WriteChars( stream, ")" );
	DString_Delete( name );
}

DaoTypeCore daoEnumCore =
{
	"enum",                                          /* name */
	{ NULL },                                        /* bases */
	NULL,                                            /* numbers */
	NULL,                                            /* methods */
	NULL,                     NULL,                  /* GetField */
	NULL,                     NULL,                  /* SetField */
	NULL,                     NULL,                  /* GetItem */
	NULL,                     NULL,                  /* SetItem */
	DaoEnum_CheckUnary,       DaoEnum_DoUnary,       /* Unary */
	DaoEnum_CheckBinary,      DaoEnum_DoBinary,      /* Binary */
	DaoEnum_CheckComparison,  DaoEnum_DoComparison,  /* Comparison */
	DaoEnum_CheckConversion,  DaoEnum_DoConversion,  /* Conversion */
	NULL,                     NULL,                  /* ForEach */
	DaoEnum_Print,                                   /* Print */
	NULL,                                            /* Slice */
	NULL,                                            /* Copy */
	DaoEnum_Delete,                                  /* Delete */
	NULL                                             /* HandleGC */
};





DaoList* DaoList_New()
{
	DaoList *self = (DaoList*) dao_calloc( 1, sizeof(DaoList) );
	DaoValue_Init( self, DAO_LIST );
	self->value = DList_New( DAO_DATA_VALUE );
	self->value->type = DAO_DATA_VALUE;
	self->ctype = NULL;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

void DaoList_Delete( DaoList *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	GC_DecRC( self->ctype );
	DaoList_Clear( self );
	DList_Delete( self->value );
	dao_free( self );
}

void DaoList_Clear( DaoList *self )
{
	DList_Clear( self->value );
}

DaoType* DaoList_GetType( DaoList *self )
{
	return self->ctype;
}

int DaoList_SetType( DaoList *self, DaoType *type )
{
	if( self->value->size || self->ctype ) return 0;
	self->ctype = type;
	GC_IncRC( type );
	return 1;
}
daoint DaoList_Size( DaoList *self )
{
	return self->value->size;
}

DaoValue* DaoList_Front( DaoList *self )
{
	if( self->value->size == 0 ) return NULL;
	return self->value->items.pValue[0];
}

DaoValue* DaoList_Back( DaoList *self )
{
	if( self->value->size == 0 ) return NULL;
	return self->value->items.pValue[ self->value->size-1 ];
}

DaoValue* DaoList_GetItem( DaoList *self, daoint pos )
{
	if( (pos = DaoList_MakeIndex( self, pos, 0 )) == -1 ) return NULL;
	return self->value->items.pValue[pos];
}

int DaoList_SetItem( DaoList *self, DaoValue *it, daoint pos )
{
	DaoValue **val;
	if( (pos = DaoList_MakeIndex( self, pos, 0 )) == -1 ) return 1;
	val = self->value->items.pValue + pos;
	if( self->ctype && self->ctype->nested->size ){
		return DaoValue_Move( it, val, self->ctype->nested->items.pType[0] ) == 0;
	}else{
		DaoValue_Copy( it, val );
	}
	return 0;
}

int DaoList_PushFront( DaoList *self, DaoValue *item )
{
	DaoType *tp = self->ctype ? self->ctype->nested->items.pType[0] : NULL;
	DaoValue *temp = NULL;
	if( DaoValue_Move( item, & temp, tp ) ==0 ){
		GC_DecRC( temp );
		return 1;
	}
	DList_PushFront( self->value, NULL );
	DaoGC_Assign2( self->value->items.pValue, temp );
	return 0;
}

int DaoList_PushBack( DaoList *self, DaoValue *item )
{
	DaoType *tp = self->ctype ? self->ctype->nested->items.pType[0] : NULL;
	DaoValue *temp = NULL;
	if( DaoValue_Move( item, & temp, tp ) ==0 ){
		GC_DecRC( temp );
		return 1;
	}
	DList_PushBack( self->value, NULL );
	DaoGC_Assign2( self->value->items.pValue + self->value->size - 1, temp );
	return 0;
}

int DaoList_Append( DaoList *self, DaoValue *value )
{
	return DaoList_PushBack( self, value );
}

int DaoList_Insert( DaoList *self, DaoValue *item, daoint pos )
{
	DaoType *tp = self->ctype ? self->ctype->nested->items.pType[0] : NULL;
	DaoValue *temp = NULL;
	if( (pos = DaoList_MakeIndex( self, pos, 1 )) == -1 ) return 1;
	if( DaoValue_Move( item, & temp, tp ) ==0 ){
		GC_DecRC( temp );
		return 1;
	}
	DList_Insert( self->value, NULL, pos );
	DaoGC_Assign2( self->value->items.pValue + pos, temp );
	return 0;
}

void DaoList_Erase( DaoList *self, daoint pos )
{
	if( pos >= self->value->size ) return;
	DList_Erase( self->value, pos, 1 );
}

void DaoList_PopFront( DaoList *self )
{
	if( self->value->size ==0 ) return;
	DList_PopFront( self->value );
}

void DaoList_PopBack( DaoList *self )
{
	if( self->value->size ==0 ) return;
	DList_PopBack( self->value );
}

DaoList* DaoList_Copy( DaoList *self, DaoType *type ) // XXX
{
	daoint i;
	DaoList *copy = DaoList_New();
	/* no detailed checking of type matching, must be ensured by caller */
	copy->ctype = (type && type->tid == DAO_LIST) ? type : self->ctype;
	GC_IncRC( copy->ctype );
	DList_Resize( copy->value, self->value->size, NULL );
	for(i=0; i<self->value->size; i++)
		DaoList_SetItem( copy, self->value->items.pValue[i], i );
	return copy;
}

static DaoType* DaoList_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	DaoType *itype = dao_type_any;

	if( type->nested->size ) itype = type->nested->items.pType[0];

	if( N == 0 ) return self;
	if( N != 1 ) return NULL;
	if( index[0]->tid == DAO_ITERATOR ){
		if( DaoType_CheckNumberIndex( index[0]->nested->items.pType[0] ) ) return dao_type_int;
	}else if( index[0]->tid == DAO_RANGE ){
		if( DaoType_CheckRangeIndex( index[0] ) ) return self;
	}else{
		if( DaoType_CheckNumberIndex( index[0] ) ) return itype;
	}
	return NULL;
}

static DaoValue* DaoList_DoGetItem( DaoValue *self, DaoValue *index[], int N, DaoProcess *proc )
{
	daoint i, pos, end, size = self->xList.value->size;
	DIndexRange range;
	DaoList *res;

	if( N == 0 ){
		res = DaoList_Copy( self, NULL );
		DaoProcess_PutValue( proc, (DaoValue*) res );
		return NULL;
	}
	if( N != 1 ) return NULL;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return NULL;
		DaoProcess_PutValue( proc, self->xList.value->items.pValue[pos] );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		pos = range.pos;
		end = range.end;
		res = DaoProcess_PutList( proc );
		DList_Resize( res->value, end - pos, NULL );
		for(i=pos; i<end; i++) DaoList_SetItem( res, self->value->items.pValue[i], i - pos );
		break;
	case DAO_ITERATOR :
		if( index[0]->xIterator.values[1]->type != DAO_INTEGER ) return NULL;
		pos = Dao_CheckNumberIndex( index[0]->xIterator.values[1]->xInteger.value, size, proc );
		index[0]->xIterator.values[0]->xBoolean.value = (pos + 1) < size;
		index[0]->xIterator.values[1]->xInteger.value = pos + 1;
		if( pos < 0 ) return NULL;
		DaoProcess_PutValue( proc, self->xList.value->items.pValue[pos] );
		break;
	}
	return NULL;
}

static int DaoList_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	DaoType *itype = dao_type_any;

	if( type->nested->size ) itype = type->nested->items.pType[0];
	if( DaoType_MatchTo( value, itype, NULL ) == 0 ) return DAO_ERROR_VALUE;

	if( N == 0 ) return DAO_OK;
	if( N != 1 ) return DAO_ERROR_INDEX;

	if( index[0]->tid == DAO_RANGE ){
		if( DaoType_CheckRangeIndex( index[0] ) ) return DAO_OK;
	}else{
		if( DaoType_CheckNumberIndex( index[0] ) ) return DAO_OK;
	}
	return DAO_ERROR_INDEX;
}

static int DaoList_DoSetItem( DaoValue *self, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	daoint i, pos, end, size = self->xList.value->size;
	DIndexRange range;

	if( N == 0 ){
		for(i=0; i<size; ++i){
			if( DaoList_SetItem( (DaoValue*) self, value, i ) ) return DAO_ERROR_VALUE;
		}
		return DAO_OK;
	}
	if( N != 1 ) return DAO_ERROR_INDEX;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return DAO_ERROR_INDEX;
		DaoList_SetItem( (DaoList*) self, value, pos );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		pos = range.pos;
		end = range.end;
		for(i=pos; i<end; ++i){
			if( DaoList_SetItem( (DaoValue*) self, value, i ) ) return DAO_ERROR_VALUE;
		}
		break;
	default: return DAO_ERROR_INDEX;
	}
	return DAO_OK;
}

static DaoType* DaoList_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code == DVM_SIZE ) return dao_type_int;
	return NULL;
}

static DaoValue* DaoList_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	if( op->code == DVM_SIZE ) DaoProcess_PutInteger( proc, self->xString.value->size );
	return NULL;
}

static DaoType* DaoList_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	if( op->code == DVM_IN && right->tid == DAO_LIST ) return dao_type_bool;
	return NULL;
}

static DaoValue* DaoList_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	DaoList *list = (DaoList*) right;
	daoint i, n;
	int C = 0;

	if( op->code != DVM_IN || right->tid != DAO_LIST ) return NULL;

	if( list->ctype && list->ctype->nested->size ){
		DaoType *itype = list->ctype->nested->items.pType[0];
		if( itype != NULL && DaoType_MatchValue( itype, left, NULL ) == 0 ) return NULL;
	}

	for(i=0,n=list->value->size; i<n; ++i){
		C = DaoValue_ComparePro( left, list->value->items.pValue[i], proc ) == 0; 
		if( C ) break;
	}
	DaoProcess_PutBoolean( proc, C );
	return NULL;
}

static DaoType* DaoList_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid == DAO_LIST ){ /* TODO: check item types; */
		return type;
	}else if( type->tid == DAO_TUPLE ){ /* TODO: check item types; */
		return type;
	}
	return NULL;
}

static DaoValue* DaoList_DoConversion( DaoValue *value, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	DaoList *self = (DaoValue*) value;
	DaoComplex buffer = {0};
	DaoValue *numvalue = (DaoValue*) & buffer;
	daoint i, size;

	if( type->tid == DAO_LIST ){
		DaoType *itype = dao_type_any;
		DaoList *list;

		if( type->nested->size == 0 ) return value; /* "list"? */
		if( type->nested->size != 1 ) return NULL;

		if( DaoType_MatchValue( type, selfv, NULL ) >= DAO_MT_EQ ) return self;

		itype = type->nested->items.pType[0];
		if( itype == NULL ) return NULL;
		if( itype->tid && itype->tid <= DAO_COMPLEX ) numvalue->type = itype->tid;

		list = DaoProcess_NewList( proc );
		GC_Assign( & list->ctype, type );

		DList_Resize( list->value, self->value->size, NULL );
		for(i=0,size=self->value->size; i<size; ++i){
			DaoValue *item = self->value->items.pValue[i];
			DaoTypeCore *core = DaoValue_GetTypeCore( item );
			if( core == NULL || core->DoCoversion == NULL ) return NULL;

			item = core->DoCoversion( item, itype, numvalue, proc );
			if( item == NULL ) return NULL;

			DaoValue_Copy( item, list->value->items.pValue + i );
		}
		return (DaoValue*) list;
	}else if( type->tid == DAO_TUPLE ){
		DaoTuple *tuple = DaoProcess_PrepareTuple( p, type, self->value->size );
		if( tuple == NULL ) return NULL;
		for(i=0; i<tuple->size; ++i){
			DaoValue *item = self->value->items.pValue[i];
			DaoTypeCore *core = DaoValue_GetTypeCore( item );
			DaoType *itype = dao_type_any;

			if( core == NULL || core->DoCoversion == NULL ) return NULL;
			if( i < type->nested->size ){
				itype = type->nested->items.pType[i];
			}else if( type->variadic ){
				itype = type->nested->items.pType[tsize];
			}
			if( itype->tid >= DAO_PAR_NAMED && itype->tid <= DAO_PAR_VALIST ){
				itype = (DaoType*) itype->aux;
			}

			if( itype->tid && itype->tid <= DAO_COMPLEX ) numvalue->type = itype->tid;
			item = core->DoCoversion( item, itype, numvalue, proc );
			if( item == NULL ) return NULL;
			DaoValue_Copy( item, tuple->values + i );
		}
		return tuple;
	}
	return NULL;
}

DaoType* DaoList_CheckForEach( DaoType *self, DaoRoutine *ctx )
{
	return dao_type_iterator_int;
}

void DaoList_DoForEach( DaoValue *self, DaoIterator *iterator, DaoProcess *proc )
{
	iterator->values[0]->xBoolean.value = self->xList.value->size > 0;
	iterator->values[0]->xInteger.value = 0;
}

static void DaoList_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DMap *inmap = cycmap;
	daoint i;

	if( cycmap == NULL ) cycmap = DHash_New(0,0);
	if( DMap_Find( cycmap, self ) ){
		DaoStream_WriteChars( stream, "{...}" );
		if( inmap == NULL ) DMap_Delete( cycmap );
		return;
	}
	DMap_Insert( cycmap, self, self );

	DaoStream_WriteChars( stream, "{ " );
	for(i=0; i<self->xList.value->size; ++i){
		DaoValue_QuotedPrint( self->xList.value->items.pValue[i], stream, cycmap, proc );
		if( (i+1) < self->xList.value->size ) DaoStream_WriteChars( stream, ", " );
	}
	DaoStream_WriteChars( stream, " }" );
	DMap_Erase( cycmap, self );
	if( inmap == NULL ) DMap_Delete( cycmap );
}


static void DaoLIST_New( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *res = p[N==2], *index = (DaoValue*)(void*)&idint;
	DaoList *list = DaoProcess_PutList( proc );
	daoint i, entry, size = p[0]->xInteger.value;
	daoint fold = N == 2;
	DaoVmCode *sect;

	if( fold ) DaoList_Append( list, res );
	if( size < 0 ){
		DaoProcess_RaiseError( proc, "Param", "Invalid parameter value" );
		return;
	}
	if( size == 0 ) return;
	sect = DaoProcess_InitCodeSection( proc, 1 + fold );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(i=fold; i<size; i++){
		idint.value = i;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, index );
		if( sect->b >1 && N ==2 ) DaoProcess_SetValue( proc, sect->a+1, res );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		DaoList_Append( list, res );
	}
	DaoProcess_PopFrame( proc );
}

static daoint DaoList_MakeIndex( DaoList *self, daoint index, int one_past_last )
{
	if( index < 0 ) index += self->value->size;
	if( (index < 0) | (index > (self->value->size - 1 + one_past_last)) ) return -1;
	return index;
}

static void DaoLIST_Insert( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	daoint size = self->value->size;
	daoint pos = DaoList_MakeIndex( self, p[2]->xInteger.value, 1 );
	DaoProcess_PutValue( proc, p[0] );
	if( pos == -1 ){
		DaoProcess_RaiseError( proc, "Index::Range", NULL );
		return;
	}
	DaoList_Insert( self, p[1], pos );
	if( size == self->value->size ) DaoProcess_RaiseError( proc, "Value", "value type" );
}

static void DaoLIST_Erase( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	daoint start = p[1]->xInteger.value;
	daoint n = p[2]->xInteger.value;
	DaoProcess_PutValue( proc, p[0] );
	DList_Erase( self->value, start, n );
}

static void DaoLIST_Clear( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	DaoList_Clear( self );
}

static void DaoLIST_Size( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	DaoProcess_PutInteger( proc, self->value->size );
}

static void DaoLIST_Resize( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	DaoValue *fill = dao_none_value;
	daoint size = p[1]->xInteger.value;
	if( self->ctype && self->ctype->nested->size )
		fill = self->ctype->nested->items.pType[0]->value;
	DList_Resize( self->value, size, fill );
}

static void DaoLIST_Resize2( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	DaoType *tp = NULL;
	DaoValue *fill = p[1];
	daoint size = p[2]->xInteger.value;
	if( self->ctype && self->ctype->nested->size )
		tp = self->ctype->nested->items.pType[0];

	fill = DaoValue_CopyContainer( fill, tp );
	if( fill != p[1] ){
		fill->xBase.trait |= DAO_VALUE_CONST; /* force copying; */
		DaoGC_IncRC( fill );
	}
	DList_Resize( self->value, size, fill );
	if( fill != p[1] ) DaoGC_DecRC( fill );
}

static int DaoList_CheckType( DaoList *self, DaoProcess *proc )
{
	daoint i, type;
	DaoValue **data = self->value->items.pValue;
	if( self->value->size == 0 ) return 0;
	type = data[0]->type;
	for(i=1; i<self->value->size; i++){
		if( type != data[i]->type ){
			DaoProcess_RaiseWarning( proc, NULL, "need list of same type of elements" );
			return 0;
		}
	}
	if( type < DAO_INTEGER || type >= DAO_ARRAY ){
		DaoProcess_RaiseWarning( proc, NULL, "need list of primitive data" );
		return 0;
	}
	return type;
}

static void DaoLIST_Max( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoTuple *tuple;
	DaoList *self = (DaoList*) p[0];
	DaoValue *res, **data = self->value->items.pValue;
	daoint i, imax, size = self->value->size;

	if( DaoList_CheckType( self, proc ) == 0 ){
		DaoProcess_PutNone( proc );
		return;
	}
	imax = 0;
	res = data[0];
	for(i=1; i<size; i++){
		if( DaoValue_ComparePro( res, data[i], proc ) <0 ){
			imax = i;
			res = data[i];
		}
	}
	tuple = DaoProcess_PutTuple( proc, 2 );
	tuple->values[1]->xInteger.value = imax;
	DaoTuple_SetItem( tuple, res, 0 );
}

static void DaoLIST_Min( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoTuple *tuple;
	DaoList *self = (DaoList*) p[0];
	DaoValue *res, **data = self->value->items.pValue;
	daoint i, imin, size = self->value->size;

	if( DaoList_CheckType( self, proc ) == 0 ){
		DaoProcess_PutNone( proc );
		return;
	}
	imin = 0;
	res = data[0];
	for(i=1; i<size; i++){
		if( DaoValue_ComparePro( res, data[i], proc ) >0 ){
			imin = i;
			res = data[i];
		}
	}
	tuple = DaoProcess_PutTuple( proc, 2 );
	tuple->values[1]->xInteger.value = imin;
	DaoTuple_SetItem( tuple, res, 0 );
}

static void DaoLIST_Sum( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	daoint i, len, type, size = self->value->size;
	DaoValue **data = self->value->items.pValue;
	type = DaoList_CheckType( self, proc );
	if( type == 0 ){
		DaoProcess_PutValue( proc, self->ctype->nested->items.pType[0]->value );
		return;
	}
	switch( type ){
	case DAO_BOOLEAN :
		{
			daoint res = 0;
			for(i=0; i<size; i++) res |= data[i]->xInteger.value;
			DaoProcess_PutBoolean( proc, res );
			break;
		}
	case DAO_INTEGER :
		{
			dao_integer res = 0;
			for(i=0; i<size; i++) res += data[i]->xInteger.value;
			DaoProcess_PutInteger( proc, res );
			break;
		}
	case DAO_FLOAT :
		{
			dao_float res = 0.0;
			for(i=0; i<size; i++) res += data[i]->xFloat.value;
			DaoProcess_PutFloat( proc, res );
			break;
		}
	case DAO_COMPLEX :
		{
			dao_complex res = { 0.0, 0.0 };
			for(i=0; i<self->value->size; i++) COM_IP_ADD( res, data[i]->xComplex.value );
			DaoProcess_PutComplex( proc, res );
			break;
		}
	case DAO_STRING :
		{
			DString *m = DaoProcess_PutChars( proc, "" );
			for(i=0,len=0; i<size; i++) len += data[i]->xString.value->size;
			DString_Reserve( m, len );
			for(i=0; i<size; i++) DString_Append( m, data[i]->xString.value );
			break;
		}
	default : break;
	}
}

static void DaoLIST_Push( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	daoint size = self->value->size;
	DaoProcess_PutValue( proc, p[0] );
	if ( p[2]->xEnum.value == 0 )
		DaoList_PushFront( self, p[1] );
	else
		DaoList_Append( self, p[1] );
	if( size == self->value->size ) DaoProcess_RaiseError( proc, "Value", "value type" );
}

static void DaoLIST_Pop( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	if( self->value->size == 0 ){
		DaoProcess_RaiseError( proc, "Value", "list is empty" );
		return;
	}
	if ( p[1]->xEnum.value == 0 ){
		DaoProcess_PutValue( proc, self->value->items.pValue[0] );
		DaoList_Erase( self, 0 );
	}else{
		DaoProcess_PutValue( proc, self->value->items.pValue[self->value->size -1] );
		DaoList_Erase( self, self->value->size -1 );
	}
}

static void DaoLIST_Join( DaoProcess *proc, DaoValue *p[], int N )
{
	int i;
	DaoList *self = & p[0]->xList;
	DaoProcess_PutValue( proc, p[0] );
	for(i=1; i<N; ++i) DList_AppendList( self->value, p[i]->xList.value );
}

static void DaoLIST_PushBack( DaoProcess *proc, DaoValue *p[], int N )
{
	int i;
	DaoList *self = & p[0]->xList;
	DaoProcess_PutValue( proc, p[0] );
	for(i=1; i<N; ++i) DaoList_Append( self, p[i] );
}

static void DaoLIST_Front( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	if( self->value->size == 0 ){
		DaoProcess_PutValue( proc, dao_none_value );
		DaoProcess_RaiseError( proc, "Value", "list is empty" );
		return;
	}
	DaoProcess_PutValue( proc, self->value->items.pValue[0] );
}

static void DaoLIST_Back( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *self = & p[0]->xList;
	if( self->value->size == 0 ){
		DaoProcess_PutValue( proc, dao_none_value );
		DaoProcess_RaiseError( proc, "Value", "list is empty" );
		return;
	}
	DaoProcess_PutValue( proc, self->value->items.pValue[ self->value->size -1 ] );
}

/*
// Quick Sort.
// Adam Drozdek: Data Structures and Algorithms in C++, 2nd Edition.
*/
static int Compare( DaoProcess *proc, int entry, int reg0, int reg1, DaoValue *v0, DaoValue *v1 )
{
	DaoValue **locs = proc->activeValues;
	DaoValue_Copy( v0, locs + reg0 );
	DaoValue_Copy( v1, locs + reg1 );
	proc->topFrame->entry = entry;
	DaoProcess_Execute( proc );
	return DaoValue_GetInteger( proc->stackValues[0] );
}

static void PartialQuickSort( DaoProcess *proc, int entry, int r0, int r1,
		DaoValue **data, daoint first, daoint last, daoint part )
{
	daoint lower=first+1, upper=last;
	DaoValue *val, *pivot;

	if( first >= last ) return;
	val = data[first];
	data[first] = data[ (first+last)/2 ];
	data[ (first+last)/2 ] = val;
	pivot = data[ first ];

	while( lower <= upper ){
		while( lower < last && Compare( proc, entry, r0, r1, data[lower], pivot ) ) lower ++;
		while( upper > first && Compare( proc, entry, r0, r1, pivot, data[upper] ) ) upper --;
		if( lower < upper ){
			val = data[lower];
			data[lower] = data[upper];
			data[upper] = val;
			upper --;
		}
		lower ++;
	}
	val = data[first];
	data[first] = data[upper];
	data[upper] = val;
	if( first+1 < upper ) PartialQuickSort( proc, entry, r0, r1, data, first, upper-1, part );
	if( upper >= part ) return;
	if( upper+1 < last ) PartialQuickSort( proc, entry, r0, r1, data, upper+1, last, part );
}

static void QuickSort( DaoProcess *self, DaoValue *data[], daoint first, daoint last, daoint part, int asc )
{
	daoint lower=first+1, upper=last;
	DaoValue *val, *pivot;

	if( first >= last ) return;
	val = data[first];
	data[first] = data[ (first+last)/2 ];
	data[ (first+last)/2 ] = val;
	pivot = data[ first ];

	while( lower <= upper ){
		if( asc ){
			while( lower < last && DaoValue_ComparePro( data[lower], pivot, self ) <0 ) lower ++;
			while( upper > first && DaoValue_ComparePro( pivot, data[upper], self ) <0 ) upper --;
		}else{
			while( lower < last && DaoValue_ComparePro( data[lower], pivot, self ) >0 ) lower ++;
			while( upper > first && DaoValue_ComparePro( pivot, data[upper], self ) >0 ) upper --;
		}
		if( lower < upper ){
			val = data[lower];
			data[lower] = data[upper];
			data[upper] = val;
			upper --;
		}
		lower ++;
	}
	val = data[first];
	data[first] = data[upper];
	data[upper] = val;
	if( first+1 < upper ) QuickSort( self, data, first, upper-1, part, asc );
	if( upper >= part ) return;
	if( upper+1 < last ) QuickSort( self, data, upper+1, last, part, asc );
}

static void DaoLIST_Sort( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoList *list = & p[0]->xList;
	DaoValue **items = list->value->items.pValue;
	daoint part = p[1 + (p[1]->type== DAO_ENUM)]->xInteger.value;
	daoint N;

	DaoProcess_PutValue( proc, p[0] );
	N = list->value->size;
	if( N < 2 ) return;
	if( part ==0 ) part = N;

	if( p[1]->type != DAO_ENUM ){
		DaoVmCode *sect = DaoProcess_InitCodeSection( proc, 2 );
		int entry = proc->topFrame->entry;
		if( sect == NULL ) return;
		if( sect->b < 2 ){
			DaoProcess_RaiseError( proc, NULL, "Two few code section parameters" );
			return;
		}
		PartialQuickSort( proc, entry, sect->a, sect->a + 1, items, 0, N-1, part );
		DaoProcess_PopFrame( proc );
		return;
	}
	QuickSort( proc, items, 0, N-1, part, p[1]->xEnum.value == 0 );
}

static void DaoLIST_BasicFunctional( DaoProcess *proc, DaoValue *p[], int npar, int funct )
{
	int direction = p[1]->xEnum.value;
	DaoList *list = & p[0]->xList;
	DaoList *list2 = NULL;
	DaoTuple *tuple = NULL;
	DaoVmCode *sect = NULL;
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue **items = list->value->items.pValue;
	DaoValue *res, *index = (DaoValue*)(void*)&idint;
	daoint entry, i, j, N = list->value->size;
	int popped = 0;
	switch( funct ){
	case DVM_FUNCT_SELECT :
	case DVM_FUNCT_COLLECT : list2 = DaoProcess_PutList( proc ); break;
	case DVM_FUNCT_APPLY : DaoProcess_PutValue( proc, p[0] ); break;
	case DVM_FUNCT_FIND : DaoProcess_PutValue( proc, dao_none_value ); break;
	}
	sect = DaoProcess_InitCodeSection( proc, 2 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(j=0; j<N; j++){
		i = direction ? N-1-j : j;
		idint.value = i;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, items[i] );
		if( sect->b >1 ) DaoProcess_SetValue( proc, sect->a+1, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		switch( funct ){
		case DVM_FUNCT_COLLECT :
			if( res->type != DAO_NONE ) DaoList_Append( list2, res );
			break;
		case DVM_FUNCT_SELECT :
			if( res->xBoolean.value ) DaoList_Append( list2, items[i] );
			break;
		case DVM_FUNCT_APPLY : DaoList_SetItem( list, res, i ); break;
		}
		if( funct == DVM_FUNCT_FIND && res->xBoolean.value ){
			popped = 1;
			DaoProcess_PopFrame( proc );
			tuple = DaoProcess_PutTuple( proc, 2 );
			GC_Assign( & tuple->values[1], items[i] );
			tuple->values[0]->xInteger.value = j;
			break;
		}
	}
	if( popped == 0 ) DaoProcess_PopFrame( proc );
}

static void DaoLIST_Collect( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_BasicFunctional( proc, p, npar, DVM_FUNCT_COLLECT );
}

static void DaoLIST_Find( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_BasicFunctional( proc, p, npar, DVM_FUNCT_FIND );
}

static void DaoLIST_Select( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_BasicFunctional( proc, p, npar, DVM_FUNCT_SELECT );
}

static void DaoLIST_Iterate( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_BasicFunctional( proc, p, npar, DVM_FUNCT_ITERATE );
}

static void DaoLIST_Apply( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_BasicFunctional( proc, p, npar, DVM_FUNCT_APPLY );
}

static void DaoLIST_Reduce( DaoProcess *proc, DaoValue *p[], int npar, int which )
{
	DaoList *list = & p[0]->xList;
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue **items = list->value->items.pValue;
	DaoValue *res = NULL, *index = (DaoValue*)(void*)&idint;
	daoint entry, i, j, first = 0, D = 0, N = list->value->size;
	DaoVmCode *sect;

	if( which == 1 ){
		res = list->value->size ? items[0] : dao_none_value;
		D = p[1]->xEnum.value;
		first = 1;
	}else{
		res= p[1];
		D = p[2]->xEnum.value;
	}
	if( list->value->size == 0 ){
		DaoProcess_PutValue( proc, res );
		return;
	}
	sect = DaoProcess_InitCodeSection( proc, 3 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(j=first; j<N; j++){
		i = D ? N-1-j : j;
		idint.value = i;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, items[i] );
		if( sect->b >1 ) DaoProcess_SetValue( proc, sect->a+1, res );
		if( sect->b >2 ) DaoProcess_SetValue( proc, sect->a+2, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
	}
	DaoProcess_PopFrame( proc );
	DaoProcess_PutValue( proc, res );
}

static void DaoLIST_Reduce1( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_Reduce( proc, p, npar, 1 );
}

static void DaoLIST_Reduce2( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_Reduce( proc, p, npar, 2 );
}

static unsigned int DaoProcess_GetHashSeed( DaoProcess *self, DaoValue *seed )
{
	if( seed->type == DAO_INTEGER ) return seed->xInteger.value;
	if( seed->type == DAO_ENUM ){
		unsigned int hashing = seed->xEnum.value;
		if( hashing == 2 ) hashing = rand();
		return hashing;
	}
	return 0;
}

static void DaoLIST_Functional2( DaoProcess *proc, DaoValue *p[], int npar, int meth )
{
	DaoValue *res = NULL;
	DaoMap *map = NULL;
	DaoVmCode *sect = NULL;
	DaoList *list3 = NULL;
	DaoList *list = & p[0]->xList;
	DaoList *list2 = & p[1]->xList;
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue **items = list->value->items.pValue;
	DaoValue **items2 = list2->value->items.pValue;
	DaoValue *index = (DaoValue*)(void*)&idint;
	daoint entry, i, j, N = list->value->size;
	unsigned int hashing = DaoProcess_GetHashSeed( proc, p[2] );
	int direction = DaoValue_TryGetEnum( p[2] );

	switch( meth ){
	case DVM_FUNCT_COLLECT :
		list3 = DaoProcess_PutList( proc );
		break;
	case DVM_FUNCT_ASSOCIATE :
		map = DaoProcess_PutMap( proc, hashing );
		direction = 0;
		break;
	}

	sect = DaoProcess_InitCodeSection( proc, 3 );
	if( sect == NULL ) return;
	if( N > list2->value->size ) N = list2->value->size;
	entry = proc->topFrame->entry;
	for(j=0; j<N; j++){
		i = direction ? N-1-j : j;
		idint.value = i;
		if( sect->b > 0 ) DaoProcess_SetValue( proc, sect->a, items[i] );
		if( sect->b > 1 ) DaoProcess_SetValue( proc, sect->a+1, items2[i] );
		if( sect->b > 2 ) DaoProcess_SetValue( proc, sect->a+2, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		if( res->type == DAO_NONE ) continue;
		switch( meth ){
		case DVM_FUNCT_COLLECT :
			DaoList_Append( list3, res );
			break;
		case DVM_FUNCT_ASSOCIATE :
			DaoMap_Insert( map, res->xTuple.values[0], res->xTuple.values[1] );
			break;
		}
	}
	DaoProcess_PopFrame( proc );
}

static void DaoLIST_Collect2( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_Functional2( proc, p, npar, DVM_FUNCT_COLLECT );
}

static void DaoLIST_Associate2( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoLIST_Functional2( proc, p, npar, DVM_FUNCT_ASSOCIATE );
}

static void DaoLIST_Associate( DaoProcess *proc, DaoValue *p[], int npar )
{
	DaoValue *res = NULL;
	DaoList *list = & p[0]->xList;
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue **items = list->value->items.pValue;
	DaoValue *index = (DaoValue*)(void*)&idint;
	DaoMap *map = DaoProcess_PutMap( proc, DaoProcess_GetHashSeed( proc, p[1] ) );
	DaoVmCode *sect = DaoProcess_InitCodeSection( proc, 2 );
	daoint entry = proc->topFrame->entry;
	daoint i, N = list->value->size;

	if( sect == NULL ) return;
	for(i=0; i<N; i++){
		idint.value = i;
		if( sect->b > 0 ) DaoProcess_SetValue( proc, sect->a, items[i] );
		if( sect->b > 1 ) DaoProcess_SetValue( proc, sect->a+1, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		if( res->type == DAO_NONE ) continue;
		DaoMap_Insert( map, res->xTuple.values[0], res->xTuple.values[1] );
	}
	DaoProcess_PopFrame( proc );
}

static DaoFunctionEntry daoListMeths[] =
{
	{ DaoLIST_New,
		"list<@T=any>( count: int )[index: int => @T] => list<@T>"
	},
	{ DaoLIST_New,
		"list<@T=any>( count: int, init: @T )[index: int, prev: @T => @T] => list<@T>"
	},
	{ DaoLIST_Clear,
		"clear( self: list<@T> )"
		/*
		// Clear the list.
		*/
	},
	{ DaoLIST_Size,
		"size( invar self: list<@T> )=>int"
		/*
		// Return the size of the list.
		*/
	},
	{ DaoLIST_Resize,
		"resize( self: list<@T<bool|int|float|complex|string|enum>>, size: int )"
		/*
		// Resize the list of primitive data to size "size".
		*/
	},
	{ DaoLIST_Resize2,
		"resize( self: list<@T>, value: @T, size: int )"
		/*
		// Resize the list to size "size", and fill the new items with value "value".
		*/
	},
	{ DaoLIST_Max,
		"max( invar self: list<@T<bool|int|float|complex|string|enum>> ) => tuple<@T,int>|none"
		/*
		// Return the maximum value of the list and its index.
		// The list has to contain primitive data.
		// In case of complex values, complex numbers are compared by the real part
		// first, and then by the imaginary part.
		*/
	},
	{ DaoLIST_Min,
		"min( invar self: list<@T<bool|int|float|complex|string|enum>> ) => tuple<@T,int>|none"
		/*
		// Return the minimum value of the list and its index.
		*/
	},
	{ DaoLIST_Sum,
		"sum( invar self: list<@T<bool|int|float|complex|string>> ) => @T"
		/*
		// Return the sum of the list.
		*/
	},
	{ DaoLIST_Insert,
		"insert( self: list<@T>, item: @T, pos = 0 ) => list<@T>"
		/*
		// Insert iten "item" as position "pos".
		// Return the self list;
		*/
	},
	{ DaoLIST_Erase,
		"erase( self: list<@T>, start = 0, count = 1 ) => list<@T>"
		/*
		// Erase from the list "count" items starting from "start".
		// Return the self list;
		*/
	},
	{ DaoLIST_Join,
		"join( self: list<@T>, other: list<@T>, ... : list<@T> ) => list<@T>"
		/*
		// Join none or more lists at the end of the list.
		// Return the self list;
		*/
	},
	{ DaoLIST_PushBack,
		"append( self: list<@T>, item: @T, ... : @T ) => list<@T>"
		/*
		// Append one or more items at the end of the list.
		// Return the self list;
		*/
	},
	{ DaoLIST_Push,
		"push( self: list<@T>, item: @T, to: enum<front, back> = $back ) => list<@T>"
		/*
		// Push an item to the list, either at the front or at the back.
		// Return the self list;
		*/
	},
	{ DaoLIST_Pop,
		"pop( self: list<@T>, from: enum<front,back> = $back ) => @T"
		/*
		// Pop off an item from the list, either from the front or from the end.
		// Return the self list;
		*/
	},
	{ DaoLIST_Front,
		"front( invar self: list<@T> ) => @T"
		/*
		// Get the front item of the list.
		*/
	},
	{ DaoLIST_Back,
		"back( invar self: list<@T> ) => @T"
		/*
		// Get the back item of the list.
		*/
	},
	{ DaoLIST_Collect,
		"collect( invar self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: @T, index: int => none|@V] => list<@V>"
		/*
		// Collect the non-"none" values produced by evaluating the code section
		// on the items of the list.
		// The iteration direction can be controlled by the "direction" parameter.
		//
		// Note: invar<@T> will not match to none|@V;
		*/
	},
	{ DaoLIST_Collect2,
		"collect( invar self: list<@T>, invar other: list<@S>, "
			"direction: enum<forward,backward> = $forward )"
			"[item: @T, item2: @S, index: int => none|@V] => list<@V>"
		/*
		// Collect the non-"none" values produced by evaluating the code section
		// on the items of the two lists.
		// The iteration direction can be controlled by the "direction" parameter.
		*/
	},
	{ DaoLIST_Associate,
		"associate( invar self: list<@T>, hashing: enum<none,auto,random>|int = $none )"
			"[item: invar<@T>, index: int => none|tuple<@K,@V>] => map<@K,@V>"
		/*
		// Iterate over this list and evaluate the code section on the item
		// value(s) and index. The code section may return none value, or a
		// pair of key and value as a tuple. These keys and values from the
		// code section will produce a map/hash (associative array) which will
		// be returned by the method.
		//
		// The last optional parameter "hashing" may take the following values:
		// -- "0" or "$none": indicating the resulting map will be ordered by keys;
		// -- "1" or "$auto": indicating the resulting map will be a hash map with
		//                    the default hashing seed;
		// -- "2" or "$random": indicating the resulting map will be a hash map with
		//                      a random hashing seed;
		// -- Other integers: indicating the resulting map will be a hash map with
		//                    this "hashing" value as the hashing seed;
		*/
	},
	{ DaoLIST_Associate2,
		"associate( invar self: list<@T>, invar other: list<@S>,"
			"hashing: enum<none,auto,random>|int = $none )"
			"[item: invar<@T>, item2: invar<@S>, index: int => none|tuple<@K,@V>]"
			"=> map<@K,@V>"
		/*
		// The same as above method except this method iterate over two lists.
		*/
	},
	{ DaoLIST_Reduce1,
		"reduce( invar self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: invar<@T>, value: @T, index: int => @T] => @T|none"
		/*
		// Reduce (fold) the items of the list.
		// The process is the following:
		// 1. The first item is taken as the initial and current value;
		// 2. Starting from the second item, each item and the current value are
		//    passed to the code section to evaluate for a new current value;
		// 3. Each new current value will be passed along with the next item
		//    to do the same code section evaluation to update the value.
		// 4. When all items are processed, the current value will be returned.
		//
		// The direction of iteration can be controlled by the "direction" paramter.
		// If the list is empty, "none" will be returned.
		*/
	},
	{ DaoLIST_Reduce2,
		"reduce( invar self: list<@T>, init: @V,"
			"direction: enum<forward,backward> = $forward )"
			"[item: invar<@T>, value: @V, index: int => @V] => @V"
		/*
		// Reduce (fold) the items of the list.
		// The process is essentially the same as the above "reduce()" method,
		// except that:
		// 1. The initial value is passed in as parameter, so the iteration will
		//    start from the first item;
		// 2. The value produced by the code section does not have to be the same
		//    as the items of the list;
		// 3. When the list is empty, the "init" value will be returned.
		*/
	},
	{ DaoLIST_Find,
		"find( invar self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: invar<@T>, index: int => bool] => tuple<index:int,value:@T> | none"
		/*
		// Find the first item in the list that meets the condition as expressed
		// by the code section. A true value of the code section indicates
		// the condition is met.
		// The direction of iteration can be controlled by the "direction" paramter.
		*/
	},
	{ DaoLIST_Select,
		"select( invar self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: invar<@T>, index: int => bool] => list<@T>"
		/*
		// Select items in the list that meets the condition as expressed
		// by the code section. A true value of the code section indicates
		// the condition is met.
		// The direction of iteration can be controlled by the "direction" paramter.
		*/
	},
	{ DaoLIST_Iterate,
		"iterate( invar self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: invar<@T>, index: int]"
		/*
		// Iterate on the list. The direction of iteration can be controlled by
		// the "direction" paramter.
		*/
	},
	{ DaoLIST_Iterate,
		"iterate( self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: @T, index: int]"
		/*
		// Iterate on the list. The direction of iteration can be controlled by
		// the "direction" paramter.
		// The only difference from the above "iterate()" method is that this
		// method cannot take constant list as the self parameter.
		*/
	},
	{ DaoLIST_Sort,
		"sort( self: list<@T>, order: enum<ascend,descend> = $ascend, part = 0 )"
			"=> list<@T>"
		/*
		// Sort the list by asceding or descending order. And stops when the
		// largest "part" items (for descending sorting) or the smallest "part"
		// items (for asceding sorting) have been correctly sorted, which also
		// means the first "part" items in the (partially) sorted list are in
		// the right positions. Zero "part" means sorting all items.
		*/
	},
	{ DaoLIST_Sort,
		"sort( self: list<@T>, part = 0 )[X: @T, Y: @T => int] => list<@T>"
		/*
		// Sort the list by ordering as defined by the code section.
		// During the sorting, two items "X" and "Y" will be passed to the code
		// section for comparison, a non-zero value produced by the code section
		// indicates "X" is less/smaller than "Y".
		// The "part" parameter has the same meaning as in the above "sort()" method.
		*/
	},
	{ DaoLIST_Apply,
		"apply( self: list<@T>, direction: enum<forward,backward> = $forward )"
			"[item: @T, index: int => @T] => list<@T>"
		/*
		// Apply new values to the items of the list. Each item and its index are
		// passed to the code section, and values produced by the code section are
		// used to replace the items of the list.
		// The direction of iteration can be controlled by the "direction" paramter.
		*/
	},
	{ NULL, NULL }
};



DaoTypeCore daoListCore =
{
	"list<@T=any>",                                  /* name */
	{ NULL },                                        /* bases */
	NULL,                                            /* numbers */
	daoListMeths,                                    /* methods */
	DaoValue_CheckGetField,   DaoValue_DoGetField,   /* GetField */
	NULL,                     NULL,                  /* SetField */
	DaoList_CheckGetItem,     DaoList_DoGetItem,     /* GetItem */
	DaoList_CheckSetItem,     DaoList_DoSetItem,     /* SetItem */
	DaoList_CheckUnary,       DaoList_DoUnary,       /* Unary */
	DaoList_CheckBinary,      DaoList_DoBinary,      /* Binary */
	NULL,                     NULL,                  /* Comparison */
	DaoList_CheckConversion,  DaoList_DoConversion,  /* Conversion */
	DaoList_CheckForEach,     DaoList_DoForEach,     /* ForEach */
	DaoList_Print,                                   /* Print */
	NULL,                                            /* Slice */
	NULL,                                            /* Copy */
	DaoList_Delete,                                  /* Delete */
	NULL                                             /* HandleGC */
};





/*
// Map and Hash Map (Associative Array):
*/
DaoMap* DaoMap_New( unsigned int hashing )
{
	DaoMap *self = (DaoMap*) dao_malloc( sizeof( DaoMap ) );
	DaoValue_Init( self, DAO_MAP );
	if( hashing ){
		self->value = DHash_New( DAO_DATA_VALUE, DAO_DATA_VALUE ); 
		if( hashing > 1 ) self->value->hashing = hashing;
	}else{
		self->value = DMap_New( DAO_DATA_VALUE, DAO_DATA_VALUE );
	}
	self->ctype = NULL;

#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

void DaoMap_Delete( DaoMap *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	GC_DecRC( self->ctype );
	DaoMap_Clear( self );
	DMap_Delete( self->value );
	dao_free( self );
}

void DaoMap_Clear( DaoMap *self )
{
	DMap_Clear( self->value );
}

void DaoMap_Reset( DaoMap *self, unsigned int hashing )
{
	DMap *map = self->value;

	DMap_Reset( self->value );
	if( hashing == 1 ) return;

	if( hashing == 0 ){
		map->hashing = 0;
		if( map->table ) dao_free( map->table );
		map->table = NULL;
		map->tsize2rt = 0;
	}else{
		if( map->hashing == 0 ){
			map->tsize2rt = 2;
			map->table = (DNode**) dao_calloc( map->tsize2rt * map->tsize2rt, sizeof(DNode*) );
		}
		map->hashing = hashing;
	}
}

DaoType* DaoMap_GetType( DaoMap *self )
{
	return self->ctype;
}

int DaoMap_SetType( DaoMap *self, DaoType *type )
{
	if( self->value->size || self->ctype ) return 0;
	self->ctype = type;
	GC_IncRC( type );
	return 1;
}

daoint DaoMap_Size( DaoMap *self )
{
	return self->value->size;
}

DaoValue* DaoMap_GetValue( DaoMap *self, DaoValue *key  )
{
	DNode *node = MAP_Find( self->value, key );
	if( node ) return node->value.pValue;
	return NULL;
}

DaoValue* DaoMap_GetValueChars( DaoMap *self, const char *key  )
{
	DaoString vkey = { DAO_STRING,0,0,0,1,NULL};
	DString str = DString_WrapChars( key );
	DNode *node;
	vkey.value = & str;
	node = MAP_Find( self->value, (DaoValue*) &  vkey );
	if( node ) return node->value.pValue;
	return NULL;
}

int DaoMap_Insert( DaoMap *self, DaoValue *key, DaoValue *value )
{
	DaoType *tp = self->ctype;
	DaoType *tp1=NULL, *tp2=NULL;
	DaoValue *key2 = NULL;
	DaoValue *value2 = NULL;
	int mt;
	if( tp ){
		if( tp->nested->size >=2 ){
			tp1 = tp->nested->items.pType[0];
			tp2 = tp->nested->items.pType[1];
		}else if( tp->nested->size >=1 ){
			tp1 = tp->nested->items.pType[0];
		}
	}
	/* type checking and setting */
	if( tp1 ){
		if( (mt = DaoType_MatchValue( tp1, key, NULL )) ==0 ) return DAO_ERROR_KEY;
		if( mt != DAO_MT_EQ ){
			if( DaoValue_Move( key, & key2, tp1 ) == 0 ){
				GC_DecRC( key2 );
				return DAO_ERROR_KEY;
			}
			key = key2;
		}
	}
	if( tp2 ){
		if( (mt = DaoType_MatchValue( tp2, value, NULL )) ==0 ) return DAO_ERROR_VALUE;
		if( mt != DAO_MT_EQ ){
			if( DaoValue_Move( value, & value2, tp2 ) == 0 ){
				GC_DecRC( value2 );
				return DAO_ERROR_VALUE;
			}
			value = value2;
		}
	}
	DMap_Insert( self->value, key, value );
	GC_DecRC( key2 );
	GC_DecRC( value2 );
	return DAO_OK;
}

int DaoMap_InsertChars( DaoMap *self, const char *key, DaoValue *value )
{
	DaoString vkey = { DAO_STRING,0,0,0,1,NULL};
	DString str = DString_WrapChars( key );
	vkey.value = & str;
	return DaoMap_Insert( self, (DaoValue*) & vkey, value );
}

static DaoValue* DaoMap_AdjustKey( DaoMap *self, DaoValue *key, DaoEnum *sym )
{
	if( key->type != DAO_ENUM || key->xEnum.subtype != DAO_ENUM_SYM ) return key;
	if( self->ctype->nested->items.pType[0]->tid == DAO_ENUM ){
		*sym = self->ctype->nested->items.pType[0]->value->xEnum;
		if( DaoEnum_SetValue( sym, (DaoEnum*) key ) ) key = (DaoValue*) sym;
	}
	return key;
}

void DaoMap_Erase( DaoMap *self, DaoValue *key )
{
	DaoEnum sym = {0};
	MAP_Erase( self->value, DaoMap_AdjustKey( self, key, & sym, NULL ) );
}

void DaoMap_EraseChars( DaoMap *self, const char *key )
{
	DaoString vkey = { DAO_STRING,0,0,0,1,NULL};
	DString str = DString_WrapChars( key );
	vkey.value = & str;
	DaoMap_Erase( self, (DaoValue*) & vkey );
}

DaoMap* DaoMap_Copy( DaoMap *self, DaoType *type )
{
	DaoMap *copy = DaoMap_New( self->value->hashing );
	DNode *node = DMap_First( self->value );
	copy->ctype = (type && type->tid == DAO_MAP) ? type : self->ctype;
	GC_IncRC( copy->ctype );
	for( ; node !=NULL; node = DMap_Next(self->value, node ))
		DaoMap_Insert( copy, node->key.pValue, node->value.pValue );
	return copy;
}

DNode* DaoMap_First( DaoMap *self )
{
	return DMap_First( self->value );
}

DNode* DaoMap_Next( DaoMap *self, DNode *iter )
{
	return DMap_Next( self->value, iter );
}

DNode* DaoMap_Find( DaoMap *self, DaoValue *key )
{
	DaoEnum sym = {0};
	return DMap_Find( self->value, DaoMap_AdjustKey( self, key, & sym ) );
}

static DNode* DaoMap_FindGE( DaoMap *self, DaoValue *key )
{
	DaoEnum sym = {0};
	key = DaoMap_AdjustKey( self, key, & sym );
	return DMap_FindPro( self->value, key, DAO_KEY_GE );
}

static DNode* DaoMap_FindLE( DaoMap *self, DaoValue *key )
{
	DaoEnum sym = {0};
	key = DaoMap_AdjustKey( self, key, & sym );
	return DMap_FindPro( self->value, key, DAO_KEY_LE );
}



static DaoType* DaoMap_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	DaoType *ketype = dao_type_any;
	DaoType *vatype = dao_type_any;

	if( N == 0 ) return self;

	if( type->nested->size     ) ketype = type->nested->items.pType[0];
	if( type->nested->size > 1 ) vatype = type->nested->items.pType[1];

	if( N != 1 ) return NULL;

	if( index[0]->tid == DAO_ITERATOR ){
		DaoType *itypes[2];
		itypes[0] = ketype;
		itypes[1] = vatype;
		return DaoNamespace_MakeType( ns, "tuple", DAO_TUPLE, NULL, types, 2 );
	}else if( index[0]->tid == DAO_RANGE ){
		DaoType *first  = index[0]->nested->items.pType[0];
		DaoType *second = index[0]->nested->items.pType[1];
		if( DaoType_MatchTo( first,  ketype, NULL ) == 0 ) return NULL;
		if( DaoType_MatchTo( second, vatype, NULL ) == 0 ) return NULL;
		return self;
	}
	if( DaoType_MatchTo( index[0], ketype, NULL ) ) return vatype;
	return NULL;
}

static DaoValue* DaoMap_DoGetItem( DaoValue *selfv, DaoValue *index[], int N, DaoProcess *proc )
{
	DaoMap *res, *self = (DaoMap*) selfv;

	if( N == 0 ){
		res = DaoMap_Copy( self, NULL );
		DaoProcess_PutValue( proc, (DaoValue*) res );
		return NULL;
	}
	if( N != 1 ) return NULL;
	if( index[0]->type == DAO_ITERATOR ){
		DaoIterator *iter = (DaoIterator*) index[0];
		DaoTuple *tuple = DaoProcess_PutTuple( proc, 2 );
		DNode *node = (DNode*) iter->values[1]->xCdata.data;

		iter->values[0]->xInteger.value = node != NULL;
		if( node == NULL || tuple->size != 2 ) return NULL;

		DaoValue_Copy( node->key.pValue, tuple->values );
		DaoValue_Copy( node->value.pValue, tuple->values + 1 );

		node = DMap_Next( self->value, node );
		iter->values[0]->xInteger.value = node != NULL;
		iter->values[1]->xCdata.data = node;

	}else if( index[0]->type == DAO_RANGE ){
		DaoValue *first = index[0]->xRange.first;
		DaoValue *second = index[0]->xRange.second;
		DNode *node1 = DMap_First( self->value );
		DNode *node2 = NULL;
		
		if( self->value->hashing ) return NULL; /* Key range not well defined for hash map; */

		res = DaoProcess_PutMap( proc, self->value->hashing ); /* Place an empty result map; */
		if( DaoValue_Compare( first, second ) >= 0 ) return NULL; /* Done; */

		if( first->type  ) node1 = DaoMap_FindGE( self, first );
		if( second->type ) node2 = DaoMap_FindLE( self, second );
		for(; node1 != node2; node1 = DMap_Next(self->value, node1 ) ){
			DaoMap_Insert( res, node1->key.pValue, node1->value.pValue );
		}
	}else{
		DaoValue *res = DaoMap_GetValue( self, index[0] );
		if( res != NULL ) DaoProcess_PutValue( proc, res );
	}
	return NULL;
}

static int DaoMap_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	DaoType *ketype = dao_type_any;
	DaoType *vatype = dao_type_any;

	if( type->nested->size     ) ketype = type->nested->items.pType[0];
	if( type->nested->size > 1 ) vatype = type->nested->items.pType[1];

	if( DaoType_MatchTo( value, vatype, NULL ) == 0 ) return DAO_ERROR_VALUE;

	if( N == 0 ) return DAO_OK;
	if( N != 1 ) return DAO_ERROR_INDEX;

	if( index[0]->tid == DAO_RANGE ){
		DaoType *first  = index[0]->nested->items.pType[0];
		DaoType *second = index[0]->nested->items.pType[1];
		if( DaoType_MatchTo( first,  ketype, NULL ) == 0 ) return DAO_ERROR_INDEX;
		if( DaoType_MatchTo( second, vatype, NULL ) == 0 ) return DAO_ERROR_INDEX;
		return DAO_OK;
	}else if( DaoType_MatchTo( index[0], ketype, NULL ) ){
		return DAO_OK;
	}
	return DAO_ERROR_INDEX;
}

static int DaoMap_DoSetItem( DaoValue *selfv, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	DaoMap *self = (DaoMap*) selfv;

	/*
	// TODO: check the following;
	// From the previous implemenetion:
	// a : tuple<string,map<string,int>> = ('',{=>});
	// duplicating the constant to assign to "a" may not set the ctype properly;
	*/

	if( N == 0 ){
		res = DaoMap_Copy( self, NULL );
		DaoProcess_PutValue( proc, (DaoValue*) res );
		return DAO_OK;
	}
	if( N != 1 ) return DAO_ERROR_INDEX;
	if( DaoType_MatchValue( vatype, value, NULL ) == 0 ) return DAO_ERROR_VALUE;

	if( index[0]->type == DAO_RANGE ){
		DaoValue *first = index[0]->xRange.first;
		DaoValue *second = index[0]->xRange.second;
		DNode *node1 = DMap_First( self->value );
		DNode *node2 = NULL;
		
		if( self->value->hashing ) return DAO_ERROR_INDEX; /* Hash key range not well defined; */
		if( DaoValue_Compare( first, second ) >= 0 ) return DAO_OK; /* Done; */

		if( first->type  ) node1 = DaoMap_FindGE( self, first );
		if( second->type ) node2 = DaoMap_FindLE( self, second );
		for(; node1 != node2; node1 = DMap_Next(self->value, node1 ) ){
			DaoValue_Move( value, & node1->value.pValue, vatype );
		}
	}else{
		return DaoMap_Insert( self, index[0], value );
	}
	return DAO_OK;
}

static DaoType* DaoMap_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code == DVM_SIZE ) return dao_type_int;
	return NULL;
}

static DaoValue* DaoMap_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	if( op->code == DVM_SIZE ) DaoProcess_PutInteger( proc, self->xString.value->size );
	return NULL;
}

static DaoType* DaoMap_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	if( op->code == DVM_IN && right->tid == DAO_LIST ) return dao_type_bool;
	return NULL;
}

static DaoValue* DaoMap_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	DaoList *list = (DaoList*) right;
	daoint i, n;
	int C = 0;

	if( op->code != DVM_IN || right->tid != DAO_LIST ) return NULL;

	if( list->ctype && list->ctype->nested->size ){
		DaoType *itype = list->ctype->nested->items.pType[0];
		if( itype != NULL && DaoType_MatchValue( itype, left, NULL ) == 0 ) return NULL;
	}

	for(i=0,n=list->value->size; i<n; ++i){
		C = DaoValue_ComparePro( left, list->value->items.pValue[i], p ) == 0; 
		if( C ) break;
	}
	DaoProcess_PutBoolean( proc, C );
	return NULL;
}

static DaoType* DaoMap_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid != DAO_MAP ) return NULL;
	return type;
}

static DaoValue* DaoMap_DoConversion( DaoValue *selfv, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	DaoMap *map, *self = (DaoMap*) selfv;
	DaoType *ketype = NULL, *vatype = NULL;
	DaoComplex buffer1 = {0};
	DaoComplex buffer2 = {0};
	DaoValue *numvalue1 = (DaoValue*) & buffer1;
	DaoValue *numvalue2 = (DaoValue*) & buffer2;

	if( type->tid != DAO_MAP ) return NULL;
	if( type->nested->size == 0 ) return value; /* "map"? */
	if( type->nested->size != 2 ) return NULL;

	if( DaoType_MatchValue( type, selfv, NULL ) >= DAO_MT_EQ ) return self;
	
	map = DaoProcess_NewMap( proc, self->value->hashing );
	GC_Assign( & map->ctype, type );

	ketype = type->nested->items.pType[0];
	vatype = type->nested->items.pType[1];
	if( ketype == NULL || vatype == NULL ) return NULL;

	if( ketype->tid && ketype->tid <= DAO_COMPLEX ){
		buffer1->type = ketype->tid;
		numvalue1 = (DaoValue*) & buffer1;
	}
	if( vatype->tid && vatype->tid <= DAO_COMPLEX ){
		buffer2->type = vatype->tid;
		numvalue2 = (DaoValue*) & buffer2;
	}

	node = DMap_First( self->value );
	for(; node!=NULL; node=DMap_Next(self->value,node) ){
		DaoValue *key = node->key.pValue;
		DaoValue *value = node->value.pValue;
		DaoTypeCore *kecore = DaoValue_GetTypeCore( key );
		DaoTypeCore *vacore = DaoValue_GetTypeCore( value );

		if( kecore == NULL || kecore->DoCoversion == NULL ) return NULL;
		if( vacore == NULL || vacore->DoCoversion == NULL ) return NULL;

		key = kecore->DoCoversion( key, ketype, numvalue1, proc );
		value = vacore->DoCoversion( value, vatype, numvalue2, proc );
		if( key == NULL || value == NULL ) return NULL;

		DMap_Insert( map->value, key, value );
	}
	return NULL;
}

DaoType* DaoMap_CheckForEach( DaoType *self, DaoRoutine *ctx )
{
	return dao_type_iterator_any;
}

void DaoMap_DoForEach( DaoValue *self, DaoIterator *iterator, DaoProcess *proc )
{
	DNode *node = DMap_First( self->xMap.value );
	DaoValue **data = iterator->values;

	iterator->values[0]->xBoolean.value = self->xMap.value->size > 0;
	if( data[1]->type != DAO_CDATA || data[1]->xCdata.ctype != dao_type_cdata ){
		/*
		// Do not use DaoWrappers_MakeCdata()!
		// DaoWrappers_MakeCdata() will make a wrapper that is unique
		// for the wrapped data "node", since the wrapped data will be
		// updated during each iteration, the correspondence between
		// wrapped data and the wrapper will be invalidated.
		// As a consequence, nested for-in loop on the same map will
		// not work!
		*/
		DaoCdata *it = DaoCdata_Wrap( dao_type_cdata, node );
		GC_Assign( & data[1], it );
	}else{
		data[1]->xCdata.data = node;
	}
}

static void DaoMap_Print( DaoValue *selfval, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DNode *node = NULL;
	DMap *inmap = cycmap;
	DaoMap *self = (DaoMap*) selfval;
	daoint i = 0, size = self->value->size;

	if( size == 0 ){
		DaoStream_WriteChars( stream, self->value->hashing ? "{->}" : "{=>}" );
		return;
	}

	if( cycmap == NULL ) cycmap = DHash_New(0,0);
	if( DMap_Find( cycmap, self ) ){
		DaoStream_WriteChars( stream, "{...}" );
		if( inmap == NULL ) DMap_Delete( cycmap );
		return;
	}
	DMap_Insert( cycmap, self, self );

	DaoStream_WriteChars( stream, "{ " );

	node = DMap_First( self->value );
	for( ; node!=NULL; node=DMap_Next(self->value,node) ){
		DaoValue_QuotedPrint( node->key.pValue, stream, cycmap, proc );
		DaoStream_WriteChars( stream, self->value->hashing ? " -> " : " => " );
		DaoValue_QuotedPrint( node->value.pValue, stream, cycmap, proc );
		if( (++i) < size ) DaoStream_WriteChars( stream, ", " );
	}
	DaoStream_WriteChars( stream, " }" );
	DMap_Erase( cycmap, self );
	if( inmap == NULL ) DMap_Delete( cycmap );
}


static void DaoMAP_New( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoInteger idint = {DAO_INTEGER,0,0,0,0,0};
	DaoValue *res, *index = (DaoValue*)(void*)&idint;
	DaoMap *map = DaoProcess_PutMap( proc, DaoProcess_GetHashSeed( proc, p[1] ) );
	daoint i, entry, size = p[0]->xInteger.value;
	DaoVmCode *sect;

	if( size < 0 ){
		DaoProcess_RaiseError( proc, "Param", "Invalid parameter value" );
		return;
	}
	if( size == 0 ) return;
	sect = DaoProcess_InitCodeSection( proc, 1 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	for(i=0; i<size; i++){
		idint.value = i;
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, index );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		if( res->type == DAO_TUPLE && res->xTuple.size == 2 )
			DaoMap_Insert2( map, res->xTuple.values[0], res->xTuple.values[1], proc );
	}
	DaoProcess_PopFrame( proc );
}

static void DaoMAP_Clear( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap_Clear( & p[0]->xMap );
}

static void DaoMAP_Reset( DaoProcess *proc, DaoValue *p[], int N )
{
	if( N == 0 ){
		DaoMap_Reset( & p[0]->xMap, 1 );
	}else if( N > 1 ){
		DMap *map = p[0]->xMap.value;
		unsigned int hashing = p[1]->xEnum.value;
		if( hashing == 2 ) hashing = rand();
		DaoMap_Reset( & p[0]->xMap, hashing );
	}
}

static void DaoMAP_Erase( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap *self = (DaoMap*) p[0];
	DNode *ml, *mg;
	DList *keys;
	int cmp;

	DaoProcess_PutValue( proc, p[0] );
	N --;
	switch( N ){
	case 0 :
		DMap_Clear( self->value ); break;
	case 1 :
		DaoMap_Erase( self, p[1] );
		break;
	case 2 :
		cmp = DaoValue_ComparePro( p[0], p[1], proc );
		if( cmp > 0 ) return;
		mg = DaoMap_FindGE( self, p[1], proc );
		ml = DaoMap_FindLE( self, p[2], proc );
		if( cmp == 0 && mg != ml ) return;
		if( mg == NULL || ml == NULL ) return;
		ml = DMap_Next( self->value, ml );
		keys = DList_New(0);
		for(; mg != ml; mg=DMap_Next(self->value, mg)) DList_Append( keys, mg->key.pVoid );
		while( keys->size ){
			MAP_Erase( self->value, keys->items.pVoid[0] );
			DList_PopFront( keys );
		}
		DList_Delete( keys );
		break;
	default : break;
	}
}

static void DaoMAP_Insert( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap *self = & p[0]->xMap;
	int c = DaoMap_Insert2( self, p[1], p[2], proc );
	DaoProcess_PutValue( proc, p[0] );
	switch( c ){
	case 1 : DaoProcess_RaiseError( proc, "Type", "key not matching" ); break;
	case 2 : DaoProcess_RaiseError( proc, "Type", "value not matching" ); break;
	}
}

static void DaoMAP_Invert( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap *self = & p[0]->xMap;
	DaoMap *inverted = DaoProcess_PutMap( proc, self->value->hashing );
	DNode *it;
	for(it=DMap_First(self->value); it; it=DMap_Next(self->value,it)){
		DMap_InsertPro( inverted->value, it->value.pValue, it->key.pValue, proc );
	}
}

static void DaoMAP_Find( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap *self = (DaoMap*) p[0];
	DNode *node = NULL;
	int errors = proc->exceptions->size;
	switch( (int)p[2]->xEnum.value ){
	case 0 : node = DaoMap_FindLE( self, p[1], proc ); break;
	case 1 : node = DaoMap_Find2( self, p[1], proc ); break;
	case 2 : node = DaoMap_FindGE( self, p[1], proc ); break;
	default : break;
	}
	if( proc->exceptions->size > errors ) return;
	if( node ){
		DaoTuple *res = DaoProcess_PutTuple( proc, 2 );
		DaoValue_Copy( node->key.pValue, res->values );
		DaoValue_Copy( node->value.pValue, res->values + 1 );
	}else{
		DaoProcess_PutValue( proc, dao_none_value );
	}
}

static void DaoMAP_Keys( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *list = DaoProcess_PutList( proc );
	DaoMap *self = & p[0]->xMap;
	DNode *it;
	for(it=DMap_First(self->value); it; it=DMap_Next(self->value,it)){
		DaoList_Append( list, it->key.pValue );
	}
}

static void DaoMAP_Values( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *list = DaoProcess_PutList( proc );
	DaoMap *self = & p[0]->xMap;
	DNode *it;
	for(it=DMap_First(self->value); it; it=DMap_Next(self->value,it)){
		DaoList_Append( list, it->value.pValue );
	}
}

static void DaoMAP_Size( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMap *self = & p[0]->xMap;
	DaoProcess_PutInteger( proc, self->value->size );
}

static void DaoMAP_Functional( DaoProcess *proc, DaoValue *p[], int N, int funct )
{
	DaoMap *self = & p[0]->xMap;
	DaoMap *map = NULL;
	DaoList *list = NULL;
	DaoTuple *tuple = NULL;
	DaoType *type = self->ctype;
	DaoVmCode *sect = NULL;
	unsigned int hashing;
	int entry, popped = 0;
	DaoValue *res;
	DNode *node;
	switch( funct ){
	case DVM_FUNCT_ASSOCIATE :
		hashing = DaoProcess_GetHashSeed( proc, p[1] );
		map = DaoProcess_PutMap( proc, hashing );
		break;
	case DVM_FUNCT_COLLECT : list = DaoProcess_PutList( proc ); break;
	case DVM_FUNCT_APPLY : DaoProcess_PutValue( proc, p[0] ); break;
	case DVM_FUNCT_FIND : DaoProcess_PutValue( proc, dao_none_value ); break;
	case DVM_FUNCT_SELECT : map = DaoProcess_PutMap( proc, self->value->hashing ); break;
	}
	sect = DaoProcess_InitCodeSection( proc, 2 );
	if( sect == NULL ) return;
	entry = proc->topFrame->entry;
	type = type && type->nested->size > 1 ? type->nested->items.pType[1] : NULL;
	for(node=DMap_First(self->value); node; node=DMap_Next(self->value,node)){
		if( sect->b >0 ) DaoProcess_SetValue( proc, sect->a, node->key.pValue );
		if( sect->b >1 ) DaoProcess_SetValue( proc, sect->a+1, node->value.pValue );
		proc->topFrame->entry = entry;
		DaoProcess_Execute( proc );
		if( proc->status == DAO_PROCESS_ABORTED ) break;
		res = proc->stackValues[0];
		switch( funct ){
		case DVM_FUNCT_ASSOCIATE :
			if( res->type != DAO_NONE ){
				DaoMap_Insert2( map, res->xTuple.values[0], res->xTuple.values[1], proc );
			}
			break;
		case DVM_FUNCT_SELECT :
			if( res->xBoolean.value ){
				DaoMap_Insert2( map, node->key.pVoid, node->value.pVoid, proc );
			}
			break;
		case DVM_FUNCT_APPLY : DaoValue_Move( res, & node->value.pValue, type ); break;
		case DVM_FUNCT_COLLECT : if( res->type != DAO_NONE ) DaoList_Append( list, res ); break;
		}
		if( funct == DVM_FUNCT_FIND && res->xBoolean.value ){
			popped = 1;
			DaoProcess_PopFrame( proc );
			tuple = DaoProcess_PutTuple( proc, 2 );
			GC_Assign( & tuple->values[0], node->key.pValue );
			GC_Assign( & tuple->values[1], node->value.pValue );
			break;
		}
	}
	if( popped == 0 ) DaoProcess_PopFrame( proc );
}

static void DaoMAP_Iterate( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_ITERATE );
}

static void DaoMAP_Find2( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_FIND );
}

static void DaoMAP_Select( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_SELECT );
}

static void DaoMAP_Associate( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_ASSOCIATE );
}

static void DaoMAP_Collect( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_COLLECT );
}

static void DaoMAP_Apply( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoMAP_Functional( proc, p, N, DVM_FUNCT_APPLY );
}

static DaoFunctionEntry daoMapMeths[] =
{
	{ DaoMAP_New,
		"map<@K=any,@V=any>( count: int, hashing: enum<none,auto,random>|int = $none )"
			"[index: int => tuple<@K,@V>] => map<@K,@V>"
		/*
		// The last optional parameter "hashing" may take the following values:
		// -- "0" or "$none": indicating the resulting map will be ordered by keys;
		// -- "1" or "$auto": indicating the resulting map will be a hash map with
		//                    the default hashing seed;
		// -- "2" or "$random": indicating the resulting map will be a hash map with
		//                      a random hashing seed;
		// -- Other integers: indicating the resulting map will be a hash map with
		//                    this "hashing" value as the hashing seed;
		*/
	},
	{ DaoMAP_Clear,
		"clear( self: map<@K,@V> )"
		/*
		// Delete all the key-value pairs from the map.
		*/
	},
	{ DaoMAP_Reset,
		"reset( self: map<@K,@V> )"
		/*
		// Remove all the key-value pairs from the map. And cache the key-value
		// nodes internally for more efficient insertion later.
		*/
	},
	{ DaoMAP_Reset,
		"reset( self: map<@K,@V>, hashing: enum<none,auto,random> )"
		/*
		// Reset the map in the same way as the above method.
		// And additionally it may change the map from ordered (hashing=none)
		// to unordered (hashing=auto/random), or unordered to ordered map.
		// A default hashing seed will be used for "auto", and a random
		// hashing seed will be used for "random".
		*/
	},
	{ DaoMAP_Erase,
		"erase( self: map<@K,@V>, from: @K ) => map<@K,@V>"
		/*
		// Erase a key and its corresponding value from the map.
		// Return self map.
		*/
	},
	{ DaoMAP_Erase,
		"erase( self: map<@K,@V>, from: @K, to: @K ) => map<@K,@V>"
		/*
		// Erase keys in the inclusive range between "from" and "to",
		// and their corresponding values from the map.
		// Return self map.
		*/
	},
	{ DaoMAP_Insert,
		"insert( self: map<@K,@V>, key: @K, value: @V ) => map<@K,@V>"
		/*
		// Insert a new key-value pair to the map.
		// Return self map.
		*/
	},
	{ DaoMAP_Invert,
		"invert( self: map<@K,@V> ) => map<@V,@K>"
		/*
		// Invert the key-value relationship.
		// Return a new map.
		*/
	},
	{ DaoMAP_Find,
		"find( invar self: map<@K,@V>, invar key: @K, comparison: enum<LE,EQ,GE> = $EQ )"
			"=> tuple<key:@K,value:@V> | none"
		/*
		// Find the key-value pair that corresponds (or is closest) to "key".
		// According to the "comparison" parameter:
		// 1. "$LE": the key must be less than or equal to the found one;
		// 2. "$EQ": the key must equal to the found one;
		// 3. "$GE": the key must be greater than or equal to the found one;
		*/
	},
	{ DaoMAP_Keys,
		"keys( invar self: map<@K,@V> ) => list<@K>"
		/*
		// Return the keys of the map as a list.
		*/
	},
	{ DaoMAP_Values,
		"values( invar self: map<@K,@V> ) => list<@V>"
		/*
		// Return the values of the map as a list.
		*/
	},
	{ DaoMAP_Size,
		"size( invar self: map<@K,@V> ) => int"
		/*
		// Return the number of key-value pairs in map.
		*/
	},
	{ DaoMAP_Iterate,
		"iterate( invar self: map<@K,@V> )[key: invar<@K>, value: invar<@V>]"
		/*
		// Iterate over the map, and execute the associated code section
		// for each key-value pair.
		*/
	},
	{ DaoMAP_Iterate,
		"iterate( self: map<@K,@V> )[key: invar<@K>, value: @V]"
		/*
		// Iterate over the map, and execute the associated code section
		// for each key-value pair.
		*/
	},
	{ DaoMAP_Collect,
		"collect( invar self: map<@K,@V> )[key: @K, value: @V => none|@T] => list<@T>"
		/*
		// Iterate over the map, and execute the associated code section
		// for each key-value pair.
		// Return a list of non-none values collected from the code section results.
		*/
	},
	{ DaoMAP_Associate,
		"associate( invar self: map<@K,@V>, hashing: enum<none,auto,random>|int = $none )"
			"[key: invar<@K>, value: invar<@V> => none|tuple<@K2,@V2>] => map<@K2,@V2>"
		/*
		// Iterate over the map, and execute the associated code section
		// for each key-value pair.
		// Return a new map that is constructed from the new key-value pairs returned
		// from the code section evaluation.
		*/
	},
	{ DaoMAP_Find2,
		"find( invar self: map<@K,@V> )[key: invar<@K>, value: invar<@V> =>bool]"
			"=> tuple<key:@K,value:@V> | none"
		/*
		// Find the first key-value pair that meets the condition as expressed
		// by the code section. A true value from the code section means the
		// condition is satisfied.
		*/
	},
	{ DaoMAP_Select,
		"select( invar self: map<@K,@V> )[key: invar<@K>, value: invar<@V> =>bool]"
			"=> map<@K,@V>"
		/*
		// Select key-value pairs that meets the condition as expressed
		// by the code section. A true from the code section means the
		// condition is satisfied.
		*/
	},
	{ DaoMAP_Apply,
		"apply( self: map<@K,@V> )[key: @K, value: @V => @V] => map<@K,@V>"
		/*
		// Iterate over the map, and execute the associated code section
		// for each key-value pair.
		// Then update the value of each pair with the value returned by
		// the code section.
		*/
	},
	{ NULL, NULL }
};


DaoTypeCore daoMapCore =
{
	"map<@K=any,@V=any>",                           /* name */
	{ NULL },                                       /* bases */
	NULL,                                           /* numbers */
	daoMapMeths,                                    /* methods */
	DaoValue_CheckGetField,   DaoValue_DoGetField,  /* GetField */
	NULL,                     NULL,                 /* SetField */
	DaoMap_CheckGetItem,      DaoMap_DoGetItem,     /* GetItem */
	DaoMap_CheckSetItem,      DaoMap_DoSetItem,     /* SetItem */
	DaoMap_CheckUnary,        DaoMap_DoUnary,       /* Unary */
	DaoMap_CheckBinary,       DaoMap_DoBinary,      /* Binary */
	NULL,                     NULL,                 /* Comparison */
	DaoMap_CheckConversion,   DaoMap_DoConversion,  /* Conversion */
	DaoMap_CheckForEach,      DaoMap_DoForEach,     /* ForEach */
	DaoMap_Print,                                   /* Print */
	NULL,                                           /* Slice */
	NULL,                                           /* Copy */
	DaoMap_Delete,                                  /* Delete */
	NULL                                            /* HandleGC */
};





/*
// Tuple:
*/

#define DAO_TUPLE_MINSIZE 2
/*
// 2 is used instead of 1, for two reasons:
// A. most often used tuples have at least two items;
// B. some builtin tuples have at least two items, and are accessed by
//    constant sub-index, compilers such Clang may complain if 1 is used.
*/

DaoTuple* DaoTuple_New( int size )
{
	int extra = size > DAO_TUPLE_MINSIZE ? size - DAO_TUPLE_MINSIZE : 0;
	DaoTuple *self = (DaoTuple*) dao_calloc( 1, sizeof(DaoTuple) + extra*sizeof(DaoValue*) );
	DaoValue_Init( self, DAO_TUPLE );
	self->size = size;
	self->ctype = NULL;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}
#if 0
DaoTuple* DaoTuple_Create( DaoType *type, int init )
{
	int i, size = type->nested->size;
	int extit = size > DAO_TUPLE_MINSIZE ? size - DAO_TUPLE_MINSIZE : 0;
	DaoType **types = type->nested->items.pType;
	DaoTuple *self = (DaoTuple*) dao_calloc( 1, sizeof(DaoTuple) + extit*sizeof(DaoValue*) );
	self->type = DAO_TUPLE;
	if( init ){
		for(i=0; i<size; i++){
			DaoType *it = types[i];
			if( it->tid == DAO_PAR_NAMED ) it = & it->aux->xType;
			if( it->tid > DAO_ENUM && it->tid != DAO_ANY && it->tid != DAO_INITYPE ) continue;
			DaoValue_Move( it->value, self->values + i, it );
		}
	}
	GC_IncRC( type );
	self->size = size;
	self->ctype = type;
	return self;
}
#else
DaoTuple* DaoTuple_Create( DaoType *type, int N, int init )
{
	int M = type->nested->size;
	int i, size = N > (M - type->variadic) ? N : (M - type->variadic);
	int extit = size > DAO_TUPLE_MINSIZE ? size - DAO_TUPLE_MINSIZE : 0;
	DaoTuple *self = (DaoTuple*) dao_calloc( 1, sizeof(DaoTuple) + extit*sizeof(DaoValue*) );
	DaoType **types;

	DaoValue_Init( self, DAO_TUPLE );
	GC_IncRC( type );
	self->size = size;
	self->ctype = type;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	if( init == 0 || M == 0 ) return self;
	types = type->nested->items.pType;
	for(i=0; i<size; i++){
		DaoType *it = i < M ? types[i] : types[M-1];
		if( it->tid == DAO_PAR_NAMED || it->tid == DAO_PAR_VALIST ) it = & it->aux->xType;
		if( it->tid >= DAO_BOOLEAN && it->tid <= DAO_ENUM ){
			DaoValue_Move( it->value, self->values + i, it );
		}
	}
	return self;
}
#endif
DaoTuple* DaoTuple_Copy( DaoTuple *self, DaoType *type )
{
	int i, n;
	DaoTuple *copy = DaoTuple_New( self->size );
	copy->subtype = self->subtype;
	copy->ctype = (type && type->tid == DAO_TUPLE) ? type : self->ctype;
	GC_IncRC( copy->ctype );
	for(i=0,n=self->size; i<n; i++) DaoTuple_SetItem( copy, self->values[i], i );
	return copy;
}
void DaoTuple_Delete( DaoTuple *self )
{
	int i;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	for(i=0; i<self->size; i++) GC_DecRC( self->values[i] );
	GC_DecRC( self->ctype );
	dao_free( self );
}

DaoType* DaoTuple_GetType( DaoTuple *self )
{
	return self->ctype;
}
int DaoTuple_SetType( DaoTuple *self, DaoType *type )
{
	if( self->size || self->ctype ) return 0;
	self->ctype = type;
	GC_IncRC( type );
	return 1;
}
int  DaoTuple_Size( DaoTuple *self )
{
	return self->size;
}
int DaoTuple_GetIndex( DaoTuple *self, DString *name )
{
	DaoType *abtp = self->ctype;
	DNode *node = NULL;
	if( abtp && abtp->mapNames ) node = MAP_Find( abtp->mapNames, name );
	if( node == NULL || node->value.pInt >= self->size ) return -1;
	return node->value.pInt;
}
void DaoTuple_SetItem( DaoTuple *self, DaoValue *it, int pos )
{
	DaoValue **val;
	if( pos < 0 ) pos += self->size;
	if( pos < 0 || pos >= self->size ) return;
	val = self->values + pos;
	if( self->ctype && pos < self->ctype->nested->size ){
		DaoType *t = self->ctype->nested->items.pType[pos];
		if( t->tid == DAO_PAR_NAMED || t->tid == DAO_PAR_VALIST ) t = & t->aux->xType;
		DaoValue_Move( it, val, t );
	}else{
		DaoValue_Copy( it, val );
	}
}
DaoValue* DaoTuple_GetItem( DaoTuple *self, int pos )
{
	if( pos <0 || pos >= self->size ) return NULL;
	return self->values[pos];
}



static int DaoTuple_CheckIndex( DaoTuple *self, DString *field, DaoProcess *proc )
{
	int id = DaoTuple_GetIndex( self, field );
	if( id <0 || id >= self->size ){
		DaoProcess_RaiseError( proc, "Field::Absent", "" );
		return -1;
	}
	return id;
}

static DaoType* DaoTuple_GetFieldType( DaoType *self, DString *field )
{
	DaoType *type;
	DNode *node = NULL;
	if( self->mapNames ) node = MAP_Find( self->mapNames, field );
	if( node == NULL || node->value.pInt >= self->nested->size ) return NULL;
	type = self->nested->items.pType[ node->value.pInt ];
	if( type->tid == DAO_PAR_NAMED || type->tid == DAO_PAR_VALIST ) type = (DaoType*)type->aux;
	return type;
}

static DaoType* DaoTuple_CheckGetField( DaoType *self, DString *field, DaoRoutine *ctx )
{
	return DaoTuple_GetFieldType( self, field );
}

static DaoValue* DaoTuple_DoGetField( DaoValue *selfv, DString *field, DaoProcess *proc )
{
	DaoTuple *self = (DaoTuple*) selfv;
	int id = DaoTuple_CheckIndex( self, name, proc );
	if( id < 0 ) return NULL;
	return self->values[id];
}

static int DaoTuple_CheckSetField( DaoType *self, DString *field, DaoType *value, DaoRoutine *ctx )
{
	DaoType *type = DaoTuple_GetFieldType( self, field );
	if( type == NULL ) return DAO_ERROR_FIELD;
	if( DaoType_MatchTo( value, type, NULL ) == 0 ) return DAO_ERROR_VALUE;
	return DAO_OK;
}

static int DaoTuple_DoSetField( DaoValue *selfv, DString *field, DaoValue *value, DaoProcess *proc )
{
	DaoTuple *self = (DaoTuple*) selfv;
	DaoType *itype, **types = self->ctype->nested->items.pType;
	int id = DaoTuple_CheckIndex( self, name, proc );
	if( id < 0 ) return DAO_ERROR_FIELD;
	itype = types[id];
	if( itype->tid == DAO_PAR_NAMED || itype->tid == DAO_PAR_VALIST ) itype = (DaoType*)itype->aux;
	if( DaoValue_Move( value, self->values + id, itype ) == 0 ) return DAO_ERROR_VALUE;
	return DAO_OK;
}

static DaoType* DaoTuple_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	DaoNamespace *NS = ctx->nameSpace;
	DaoType *retype = dao_type_any;
	DaoType *itypes[2];
	DList *TS;
	int j;

	if( type->nested->size ) itype = type->nested->items.pType[0];

	if( N == 0 ) return self;
	if( N != 1 ) return NULL;

	if( index[0]->tid == DAO_RANGE ){
		return dao_type_tuple;
	}else if( index[0]->tid == DAO_ITERATOR ){
		if( DaoType_CheckNumberIndex( index[0]->nested->items.pType[0] ) == 0 ) return NULL;
		if( self->nested->size == 0 ) return NULL;

		retype = self->nested->items.pType[0];
		if( retype->tid >= DAO_PAR_NAMED && retype->tid <= DAO_PAR_VALIST ){
			retype = (DaoType*) retype->aux;
		}
		for(j=1; j<self->nested->size; ++j){
			DaoType *itype = self->nested->items.pType[j];
			if( itype->tid >= DAO_PAR_NAMED && itype->tid <= DAO_PAR_VALIST ){
				itype = (DaoType*) itype->aux;
			}
			if( DaoType_MatchTo( itype, retype, NULL ) < DAO_MT_EQ ){
				retype = dao_type_any;
				break;
			}
		}
		itypes[0] = dao_type_string;
		itypes[1] = retype;
		retype = DaoNamespace_MakeType( NS, "tuple", DAO_TUPLE, NULL, itypes, 2 );
	}else{
		if( DaoType_CheckNumberIndex( index[0] ) == 0 ) return NULL;

		TS = DList_New(0);
		DaoType_ExportArguments( self, TS, 1 );
		retype = DaoNamespace_MakeType( NS, "", DAO_VARIANT, NULL, TS->items.pType, TS->size );
		DList_Delete( TS );
	}
	return retype;
}

DaoTuple* DaoProcess_GetTuple( DaoProcess *self, DaoType *type, int size, int init );

static DaoValue* DaoTuple_DoGetItem( DaoValue *self, DaoValue *index[], int N, DaoProcess *proc )
{
	int i, pos, end, size = self->xTuple.size;
	DIndexRange range;
	DaoTuple *res;

	if( N == 0 ){
		res = DaoTuple_Copy( self, NULL );
		DaoProcess_PutValue( proc, (DaoValue*) res );
		return NULL;
	}
	if( N != 1 ) return NULL;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return NULL;
		DaoProcess_PutValue( proc, self->xTuple.values[pos] );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		pos = range.pos;
		end = range.end;
		if( pos == 0 && end == size ){
			DaoTuple *tuple = DaoProcess_GetTuple( proc, self->ctype, self->size, 1 ); 
			for(i=0; i<size; ++i) DaoTuple_SetItem( tuple, self->values[i], i ); 
		}else{
			DaoTuple *tuple = DaoProcess_GetTuple( proc, NULL, end - pos, 0 );
			DaoType *type = proc->activeTypes[ proc->activeCode->c ];
			if( type->tid != DAO_TUPLE ) type = dao_type_tuple;
			GC_Assign( & tuple->ctype, type );
			for(i=pos; i<end; ++i) DaoTuple_SetItem( tuple, self->values[i], i - pos);
		}
		break;
	case DAO_ITERATOR :
		if( index[0]->xIterator.values[1]->type != DAO_INTEGER ) return NULL;
		pos = Dao_CheckNumberIndex( index[0]->xIterator.values[1]->xInteger.value, size, proc );
		index[0]->xIterator.values[0]->xBoolean.value = (pos + 1) < size;
		index[0]->xIterator.values[1]->xInteger.value = pos + 1;
		if( pos < 0 ) return NULL;

		res = DaoProcess_PutTuple( proc, 2 );
		DaoValue_Move( self->values[id], & res->values[1], NULL );
		DString_Reset( res->values[0]->xString.value, 0 );
		if( pos < self->ctype->nested->size ){
			DaoType *itype = self->ctype->nested->items.pType[pos];
			if( itype->tid == DAO_PAR_NAMED ){
				DString_Assign( res->values[0]->xString.value, itype->fname );
			}
		}
		break;
	}
	return NULL;
}

static int DaoTuple_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	DaoType *itype = dao_type_any;

	if( type->nested->size ) itype = type->nested->items.pType[0];
	if( DaoType_MatchTo( value, itype, NULL ) == 0 ) return DAO_ERROR_VALUE;

	if( N != 1 ) return DAO_ERROR_INDEX;
	switch( index[0]->tid ){
	case DAO_NONE :
		break;
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT   :
		break;
	case DAO_RANGE :
		if( index[0]->nested->items.pType[0]->tid > DAO_FLOAT ) return DAO_ERROR_INDEX;
		if( index[0]->nested->items.pType[1]->tid > DAO_FLOAT ) return DAO_ERROR_INDEX;
		if( value->tid != DAO_STRING ) return DAO_ERROR_VALUE;
		break;
	default: return DAO_ERROR_INDEX;
	}
	return DAO_OK;
}

static int DaoTuple_DoSetItem( DaoValue *self, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	int i, pos, end, size = self->xTuple.size;
	DIndexRange range;

	if( N == 0 ){
		for(i=0; i<size; ++i){
			if( DaoTuple_SetItem( (DaoValue*) self, value, i ) ) return DAO_ERROR_VALUE;
		}
		return DAO_OK;
	}
	if( N != 1 ) return DAO_ERROR_INDEX;
	switch( index[0]->type ){
	case DAO_BOOLEAN :
	case DAO_INTEGER :
	case DAO_FLOAT :
		pos = DaoValue_GetInteger( index[0] );
		pos = Dao_CheckNumberIndex( pos, size, proc );
		if( pos < 0 ) return DAO_ERROR_INDEX;
		DaoTuple_SetItem( (DaoList*) self, value, pos );
		break;
	case DAO_RANGE :
		range = Dao_CheckRangeIndex( (DaoRange*) index[0], proc );
		if( range.pos < 0 ) return NULL;
		for(i=range.pos; i<range.end; ++i){
			if( DaoTuple_SetItem( (DaoValue*) self, value, i ) ) return DAO_ERROR_VALUE;
		}
		break;
	default: return DAO_ERROR_INDEX;
	}
	return DAO_OK;
}

static DaoType* DaoTuple_CheckUnary( DaoType *type, DaoVmCode *op, DaoRoutine *ctx )
{
	if( op->code == DVM_SIZE ) return dao_type_int;
	return NULL;
}

static DaoValue* DaoTuple_DoUnary( DaoValue *value, DaoVmCode *op, DaoProcess *proc )
{
	if( op->code == DVM_SIZE ) DaoProcess_PutInteger( proc, self->xTuple.size );
	return NULL;
}

static DaoType* DaoTuple_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoType *left = args[0];
	DaoType *right = args[1];

	if( left->tid != DAO_TUPLE && right->tid != DAO_TUPLE ) return NULL;
	switch( op->code ){
	case DVM_LT :
	case DVM_LE :
	case DVM_EQ :
	case DVM_NE : return dao_type_bool;
	}
	return NULL;
}

static DaoValue* DaoTuple_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoValue *left  = args[0];
	DaoValue *right = args[1];
	int C = 0;

	if( left->type != DAO_TUPLE && right->type != DAO_TUPLE ) return NULL;

	C = DaoValue_Compare( left, right );
	switch( op->code ){
	case DVM_LT : C = C <  0; break;
	case DVM_LE : C = C <= 0; break;
	case DVM_EQ : C = C == 0; break;
	case DVM_NE : C = C != 0; break;
	}
	DaoProcess_PutBoolean( proc, C );
	return NULL;
}

static DaoType* DaoTuple_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	if( type->tid == DAO_TUPLE ){
		return type;
	}else if( type->tid == DAO_LIST ){
		return type;
	}
	return NULL;
}

static DaoValue* DaoTuple_DoConversion( DaoValue *selfv, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	DaoTuple *tuple;
	DaoTuple *self = (DaoTuple*) selfv;
	DaoComplex buffer = {0};
	DaoValue *numvalue = (DaoValue*) & buffer;

	if( type->tid == DAO_TUPLE ){
		int i, typeSize = type->nested->size - type->variadic;
		if( self->ctype == type || type->nested->size == 0 ) return selfv;
		tuple = DaoProcess_PrepareTuple( proc, type, self->size );
		if( tuple == NULL ) return NULL;
		for(i=0; i<tuple->size; i++){
			DaoValue *item = self->values[i];
			DaoTypeCore *core = DaoValue_GetTypeCore( item );
			DaoType *itype = dao_type_any;
			if( core == NULL || core->DoCoversion == NULL ) return NULL;
			if( i < type->nested->size ){
				itype = type->nested->items.pType[i];
			}else if( type->variadic ){
				itype = type->nested->items.pType[typeSize];
			}
			if( itype->tid >= DAO_PAR_NAMED && itype->tid <= DAO_PAR_VALIST ){
				itype = (DaoType*) itype->aux;
			}
			if( itype->tid && itype->tid <= DAO_COMPLEX ) numvalue->type = itype->tid;
			item = core->DoCoversion( item, itype, numvalue, proc );
			if( item == NULL ) return NULL;
			DaoValue_Copy( item, tuple->values + i );
		}
		return (DaoValue*) tuple;
	}else if( type->tid == DAO_LIST ){
		DaoType *itype = dao_type_any;
		DaoList *list;

		if( type->nested->size == 0 ) return value; /* "list"? */
		if( type->nested->size != 1 ) return NULL;

		itype = type->nested->items.pType[0];
		if( itype == NULL ) return NULL;
		if( itype->tid && itype->tid <= DAO_COMPLEX ) numvalue->type = itype->tid;

		list = DaoProcess_NewList( proc );
		GC_Assign( & list->ctype, type );

		DList_Resize( list->value, self->value->size, NULL );
		for(i=0; i<self->size; ++i){
			DaoValue *item = self->values[i];
			DaoTypeCore *core = DaoValue_GetTypeCore( item );
			if( core == NULL || core->DoCoversion == NULL ) return NULL;

			item = core->DoCoversion( item, itype, numvalue, proc );
			if( item == NULL ) return NULL;

			DaoValue_Copy( item, list->value->items.pValue + i );
		}
		return (DaoValue*) list;
	}
	return NULL;
}

DaoType* DaoTuple_CheckForEach( DaoType *self, DaoRoutine *ctx )
{
	return dao_type_iterator_int;
}

void DaoTuple_DoForEach( DaoValue *self, DaoIterator *iterator, DaoProcess *proc )
{
	iterator->values[0]->xBoolean.value = self->xTuple.size > 0;
	iterator->values[0]->xInteger.value = 0;
}

static void DaoTuple_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DMap *inmap = cycmap;
	int i;

	if( cycmap == NULL ) cycmap = DHash_New(0,0);
	if( DMap_Find( cycmap, self ) ){
		DaoStream_WriteChars( stream, "(...)" );
		if( inmap == NULL ) DMap_Delete( cycmap );
		return;
	}
	DMap_Insert( cycmap, self, self );

	DaoStream_WriteChars( stream, "( " );
	for(i=0; i<self->xTuple.size; ++i){
		DaoValue_QuotedPrint( self->xTuple.values[i], stream, cycmap, proc );
		if( (i+1) < self->xTuple.size ) DaoStream_WriteChars( stream, ", " );
	}
	DaoStream_WriteChars( stream, " )" );
	DMap_Erase( cycmap, self );
	if( inmap == NULL ) DMap_Delete( cycmap );
}


DaoTypeCore daoTupleCore =
{
	"tuple",                                           /* name */
	{ NULL },                                          /* bases */
	NULL,                                              /* numbers */
	NULL,                                              /* methods */
	DaoValue_CheckGetField,    DaoValue_DoGetField,    /* GetField */
	DaoValue_CheckSetField,    DaoValue_DoSetField,    /* SetField */
	DaoTuple_CheckGetItem,     DaoTuple_DoGetItem,     /* GetItem */
	DaoTuple_CheckSetItem,     DaoTuple_DoSetItem,     /* SetItem */
	DaoTuple_CheckUnary,       DaoTuple_DoUnary,       /* Unary */
	NULL,                      NULL,                   /* Binary */
	DaoTuple_CheckComparison,  DaoTuple_DoComparison,  /* Comparison */
	DaoTuple_CheckConversion,  DaoTuple_DoConversion,  /* Conversion */
	DaoTuple_CheckForEach,     DaoTuple_DoForEach,     /* ForEach */
	DaoTuple_Print,                                    /* Print */
	NULL,                                              /* Slice */
	NULL,                                              /* Copy */
	DaoTuple_Delete,                                   /* Delete */
	NULL                                               /* HandleGC */
};





DaoIterator* DaoIterator_New( DaoType *type )
{
}

void DaoIterator_Delete( DaoIterator *self )
{
}

void DaoIterator_SetValue( DaoIterator *self, DaoValue *value )
{
}


DaoTypeCore daoIteratorCore =
{
	"Iterator",          /* name */
	{ NULL },            /* bases */
	NULL,                /* numbers */
	NULL,                /* methods */
	NULL,  NULL,         /* GetField */
	NULL,  NULL,         /* SetField */
	NULL,  NULL,         /* GetItem */
	NULL,  NULL,         /* SetItem */
	NULL,  NULL,         /* Unary */
	NULL,  NULL,         /* Binary */
	NULL,  NULL,         /* Comparison */
	NULL,  NULL,         /* Conversion */
	NULL,  NULL,         /* ForEach */
	DaoIterator_Print,   /* Print */
	NULL,                /* Slice */
	NULL,                /* Copy */
	DaoIterator_Delete,  /* Delete */
	NULL                 /* HandleGC */
};





DaoRange* DaoRange_New( DaoType *type )
{
}

void DaoRange_Delete( DaoRange *self )
{
}

void DaoRange_SetFirst( DaoRange *self, DaoValue *value )
{
}

void DaoRange_SetSecond( DaoRange *self, DaoValue *value )
{
}


DaoTypeCore daoRangeCore =
{
	"Range",          /* name */
	{ NULL },         /* bases */
	NULL,             /* numbers */
	NULL,             /* methods */
	NULL,  NULL,      /* GetField */
	NULL,  NULL,      /* SetField */
	NULL,  NULL,      /* GetItem */
	NULL,  NULL,      /* SetItem */
	NULL,  NULL,      /* Unary */
	NULL,  NULL,      /* Binary */
	NULL,  NULL,      /* Comparison */
	NULL,  NULL,      /* Conversion */
	NULL,  NULL,      /* ForEach */
	DaoRange_Print,   /* Print */
	NULL,             /* Slice */
	NULL,             /* Copy */
	DaoRange_Delete,  /* Delete */
	NULL              /* HandleGC */
};





DaoNameValue* DaoNameValue_New( DString *name, DaoValue *value )
{
	DaoNameValue *self = (DaoNameValue*)dao_calloc( 1, sizeof(DaoNameValue) );
	DaoValue_Init( self, DAO_PAR_NAMED );
	self->name = DString_Copy( name );
	DaoValue_Copy( value, & self->value );
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

void DaoNameValue_Delete( DaoNameValue *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	DString_Delete( self->name );
	DaoValue_Clear( & self->value );
	GC_DecRC( self->ctype );
	dao_free( self );
}

static void DaoNameValue_Print( DaoValue *selfv, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoNameValue *self = (DaoNameValue) selfv;
	DaoStream_WriteString( stream, self->name );
	DaoStream_WriteChars( stream, "=" );
	if( self->value && self->value->type == DAO_STRING ) DaoStream_WriteChar( stream, '"' );
	DaoValue_Print( self->value, proc, stream, cycmap );
	if( self->value && self->value->type == DAO_STRING ) DaoStream_WriteChar( stream, '"' );
}

DaoTypeCore daoNameValueCore =
{
	"NameValue",          /* name */
	{ NULL },             /* bases */
	NULL,                 /* numbers */
	NULL,                 /* methods */
	NULL,  NULL,          /* GetField */
	NULL,  NULL,          /* SetField */
	NULL,  NULL,          /* GetItem */
	NULL,  NULL,          /* SetItem */
	NULL,  NULL,          /* Unary */
	NULL,  NULL,          /* Binary */
	NULL,  NULL,          /* Comparison */
	NULL,  NULL,          /* Conversion */
	NULL,  NULL,          /* ForEach */
	DaoNameValue_Print,   /* Print */
	NULL,                 /* Slice */
	NULL,                 /* Copy */
	DaoNameValue_Delete,  /* Delete */
	NULL                  /* HandleGC */
};




/*
// DaoCstruct:
*/
void DaoCstruct_Init( DaoCstruct *self, DaoType *type )
{
	DaoType *intype = type;
	if( type == NULL ) type = dao_type_cdata;
	DaoValue_Init( self, type ? type->tid : DAO_CDATA );
	self->object = NULL;
	self->ctype = type;
	if( self->ctype ) GC_IncRC( self->ctype );
#ifdef DAO_USE_GC_LOGGER
	if( intype != NULL ) DaoObjectLogger_LogNew( (DaoValue*) self );
#endif

}

void DaoCstruct_Free( DaoCstruct *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	if( self->ctype && !(self->trait & DAO_VALUE_BROKEN) ) GC_DecRC( self->ctype );
	if( self->object ) GC_DecRC( self->object );
	self->object = NULL;
	self->ctype = NULL;
}


/*
// Note:
// Operator .( field: string ) is no longer supported;
// Overload [] instead;
*/
DaoType* DaoCstruct_CheckGetField( DaoType *self, DString *name, DaoRoutine *ctx )
{
	DaoRoutine *rout;
	DaoValue *value = DaoType_FindValue( self, name );
	DaoType *res = NULL;

	if( value && value->type == DAO_ROUTINE ){
		rout = (DaoRoutine*) value;
		res = rout->routType;
	}else if( value ){
		res = DaoNamespace_GetType( ns, value );
	}else{
		DString *buffer = DString_NewChars( "." );
		DString_Append( buffer, name );
		rout = DaoType_FindFunction( self, buffer );
		DString_Delete( buffer );
		if( rout != NULL ) rout = DaoRoutine_MatchByType( rout, self, NULL, 0, DVM_CALL );
		if( rout == NULL ) return NULL;
		res = (DaoType*) rout->routType->aux;
	}
	return res;
}

DaoValue* DaoCstruct_DoGetField( DaoValue *self, DString *name, DaoProcess *proc )
{
	DaoType *type = self->xCstruct.ctype;
	DaoValue *value = DaoType_FindValue( type, name );
	DaoRoutine *rout = NULL;

	if( value != NULL ) return value;

	DString_SetChars( proc->string, "." );
	DString_Append( proc->string, name );
	rout = DaoType_FindFunction( type, proc->string );
	if( rout == NULL ) return NULL;
	DaoProcess_PushCallable( proc, rout, self, NULL, 0 );
	return NULL;
}

int DaoCstruct_CheckSetField( DaoType *self, DString *name, DaoType *value, DaoRoutine *ctx )
{
	DaoRoutine *rout;
	DString *buffer = DString_NewChars( "." );
	DString_Append( buffer, name );
	DString_AppendChars( buffer, "=" );
	rout = DaoType_FindFunction( self, buffer );
	DString_Delete( buffer );
	if( rout == NULL ) return DAO_ERROR_FIELD_ABSENT;
	rout = DaoRoutine_MatchByType( rout, self, & value, 1, DVM_CALL );
	if( rout == NULL ) return DAO_ERROR_VALUE;
	return DAO_OK;
}

int DaoCstruct_DoSetField( DaoValue *self, DString *name, DaoValue *value, DaoProcess *proc )
{
    DaoRoutine *rout = NULL;
	DaoType *type = self->xCstruct.ctype;

    DString_SetChars( proc->string, "." );
    DString_Append( proc->string, name );
    DString_AppendChars( proc->string, "=" );
    rout = DaoType_FindFunction( type, proc->string );
    if( rout == NULL ) return DAO_ERROR_FIELD_ABSENT;
    DaoProcess_PushCallable( proc, rout, self, & value, 1 );
	return DAO_OK;
}

DaoType* DaoCstruct_CheckGetItem( DaoType *self, DaoType *index[], int N, DaoRoutine *ctx )
{
	DaoRoutine *rout = DaoType_FindFunctionChars( self, "[]" );
	if( rout != NULL ) rout = DaoRoutine_MatchByType( rout, self, index, N, DVM_CALL );
	if( rout == NULL ) return DAO_ERROR_INDEX;
	return (DaoType*) rout->routType->aux;
}

DaoValue* DaoCstruct_DoGetItem( DaoValue *self, DaoValue *index[], int N, DaoProcess *proc )
{
	DaoType *type = self->xCstruct.ctype;
	DaoRoutine *rout = DaoType_FindFunctionChars( type, "[]" );
	if( rout != NULL ) DaoProcess_PushCallable( proc, rout, self, index, N );
	return NULL;
}

int DaoCstruct_CheckSetItem( DaoType *self, DaoType *index[], int N, DaoType *value, DaoRoutine *ctx )
{
	DaoRoutine *rout = DaoType_FindFunctionChars( self, "[]=" );
	DaoType *args[ DAO_MAX_PARAM + 1 ];

	args[0] = value;
	memcpy( args + 1, index, N*sizeof(DaoType*) );

	if( rout != NULL ) rout = DaoRoutine_MatchByType( rout, self, args, N+1, DVM_CALL );
	if( rout == NULL ) return DAO_ERROR_INDEX;
	return DAO_OK;
}

int DaoCstruct_DoSetItem( DaoValue *self, DaoValue *index[], int N, DaoValue *value, DaoProcess *proc )
{
	DaoType *type = self->xCstruct.ctype;
	DaoRoutine *rout = DaoType_FindFunctionChars( type, "[]=" );
	DaoValue *args[ DAO_MAX_PARAM ];
	if( rout == NULL ) return DAO_ERROR_INDEX;
	args[0] = value;
	memcpy( args+1, index, N*sizeof(DaoValue*) );
	DaoProcess_PushCallable( proc, rout, self, args, N+1 );
	return DAO_OK;
}

DaoType* DaoCstruct_CheckUnary( DaoType *self, DaoVmCode *op, DaoRoutine *ctx )
{
	DaoRoutine *rout = NULL;

	switch( op->code ){
	case DVM_NOT   :
	case DVM_MINUS :
	case DVM_TILDE :
	case DVM_SIZE  : break;
	default: return NULL;
	}
	rout = DaoType_FindOperator( self, op->code );
	if( rout == NULL ) return NULL;
	if( op->c == op->a ){
		rout = DaoRoutine_MatchByType( rout, self, & self, 1, DVM_CALL );
	}else{
		rout = DaoRoutine_MatchByType( rout, NULL, & self, 1, DVM_CALL );
	}
	if( rout == NULL ) return NULL;
	return (DaoType*) rout->routType->aux;
}

DaoValue* DaoCstruct_DoUnary( DaoValue *self, DaoVmCode *op, DaoProcess *proc )
{
	DaoType *type = self->xCstruct.ctype;
	DaoRoutine *rout = NULL;
	int retc = 0;

	switch( op->code ){
	case DVM_NOT   :
	case DVM_MINUS :
	case DVM_TILDE :
	case DVM_SIZE  : break;
	default: return NULL;
	}
	rout = DaoType_FindOperator( type, op->code );
	if( rout == NULL ) return NULL;
	if( op->c == op->a ){
		retc = DaoProcess_PushCallable( proc, rout, self, & self, 1 );
	}else{
		retc = DaoProcess_PushCallable( proc, rout, NULL, & self, 1 );
	}
	// TODO: retc;
	return NULL;
}

#if 0
// Should used outside!
	/* Determine which type to use for searching operator overloading: */
	if( left->tid == DAO_CSTRUCT && right->tid == DAO_CSTRUCT ){
		if( op->c == op->a ){
			hostype = left;
		}else if( op->c == op->b ){
			hostype = right;
		}else if( DaoType_ChildOf( right, left ) ){
			hostype = right;
		}else{
			hostype = left;
		}
	}else if( left->tid == DAO_CSTRUCT ){
		hostype = left;
	}else if( right->tid == DAO_CSTRUCT ){
		hostype = right;
	}else{
		return NULL;
	}
#endif

DaoType* DaoCstruct_CheckBinary( DaoType *self, DaoVmCode *op, DaoType *args[2], DaoRoutine *ctx )
{
	DaoRoutine *rout = NULL;
	DaoType *selftype = NULL;

	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
	case DVM_MUL : case DVM_DIV :
	case DVM_MOD : case DVM_POW :
	case DVM_BITAND : case DVM_BITOR  :
	case DVM_AND : case DVM_OR :
	case DVM_LT  : case DVM_LE :
	case DVM_EQ  : case DVM_NE :
	case DVM_IN :
		break;
	default: return NULL;
	}
	rout = DaoType_FindOperator( self, op->code );
	if( rout == NULL ) return NULL;

	args[0] = left;
	args[1] = right;
	if( op->c == op->a && self == left  ) selftype = self;
	if( op->c == op->b && self == right ) selftype = self;
	rout = DaoRoutine_MatchByType( rout, selftype, args, 2, DVM_CALL );
	if( rout == NULL ) return NULL;
	return (DaoType*) rout->routType->aux;
}

#if 0
	/* Determine which value to use for searching operator overloading: */
	if( left->type == DAO_CSTRUCT && right->type == DAO_CSTRUCT ){
		if( op->c == op->a ){
			hostvalue = left;
		}else if( op->c == op->b ){
			hostvalue = right;
		}else if( DaoType_ChildOf( right, left ) ){
			hostvalue = right;
		}else{
			hostvalue = left;
		}
	}else if( left->type == DAO_CSTRUCT ){
		hostvalue = left;
	}else if( right->type == DAO_CSTRUCT ){
		hostvalue = right;
	}else{
		return NULL;
	}
#endif

DaoValue* DaoCstruct_DoBinary( DaoValue *self, DaoVmCode *op, DaoValue *args[2], DaoProcess *proc )
{
	DaoRoutine *rout = NULL;
	DaoValue *selfvalue = NULL;

	switch( op->code ){
	case DVM_ADD : case DVM_SUB :
	case DVM_MUL : case DVM_DIV :
	case DVM_MOD : case DVM_POW :
	case DVM_BITAND : case DVM_BITOR  :
	case DVM_AND : case DVM_OR :
	case DVM_LT  : case DVM_LE :
	case DVM_EQ  : case DVM_NE :
	case DVM_IN :
		break;
	default: return NULL;
	}
	rout = DaoType_FindOperator( self->xCstruct.ctype, op->code );
	if( rout == NULL ) return NULL;

	args[0] = left;
	args[1] = right;
	if( op->c == op->a && self == left  ) selfvalue = self;
	if( op->c == op->b && self == right ) selfvalue = self;
	retc = DaoProcess_PushCallable( proc, rout, selfvalue, args, 2 );
	// TODO: retc;
	return NULL;
}

int DaoCstruct_CheckComparison( DaoType *self, DaoType *other, DaoRoutine *ctx )
{
	return DAO_OK;
}

int DaoCstruct_DoComparison( DaoValue *self, DaoValue *other, DaoProcess *proc )
{
	if( self == other ) return 0;
	return self < other ? -1 : 1;
}

DaoType* DaoCstruct_CheckConversion( DaoType *self, DaoType *type, DaoRoutine *ctx )
{
	DString *buffer;
	DaoRoutine *rout;

	if( DaoType_ChildOf( self, type ) ) return type;
	if( DaoType_ChildOf( type, self ) ) return type;

	buffer = DString_NewChars( "(" );
	DString_Append( buffer, type->name );
	DString_AppendChars( buffer, ")" );
	rout = DaoType_FindFunction( self, buffer );
	DString_Delete( buffer );
	if( rout != NULL ){
		DaoType *ttype = DaoNamespace_GetType( ctx->nameSpace );
		rout = DaoRoutine_MatchByType( rout, self, & ttype, 1, DVM_CALL );
	}
	if( rout != NULL ) return type;
	return NULL;
}

DaoValue* DaoCstruct_DoConversion( DaoValue *self, DaoType *type, DaoValue *num, DaoProcess *proc )
{
	DaoRoutine *rout;
	DString *buffer;

	if( DaoType_MatchToParent( self->xCtruct.ctype, type, NULL, 0 ) ) return self;

	buffer = DString_NewChars( "(" );
	DString_Append( buffer, type->name );
	DString_AppendChars( buffer, ")" );
	rout = DaoType_FindFunction( self->xCstruct.ctype, buffer );
	DString_Delete( buffer );
	if( rout != NULL ){
		int rc = DaoProcess_PushCallable( proc, rout, self, & type, 1 );
		if( rc ) return NULL;
	}
	return NULL;
}

void DaoCstruct_Print( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	int ec = 0;
	char buf[50];
	DaoRoutine *meth;
	DaoValue *args[2];
	DaoType *type = self->xCstruct.ctype;
	DMap *inmap = cycmap;

	sprintf( buf, "[%p]", self );

	if( self == self->xCstruct.ctype->value ){
		DaoStream_WriteString( stream, cstruct->ctype->name );
		DaoStream_WriteChars( stream, "[default]" );
		return;
	}
	if( cycmap != NULL && DMap_Find( cycmap, self ) != NULL ){
		DaoStream_WriteString( stream, type->name );
		DaoStream_WriteChars( stream, buf );
		return;
	}

	if( cycmap == NULL ) cycmap = DHash_New(0,0);
	if( cycmap ) MAP_Insert( cycmap, self, self );

	args[0] = (DaoValue*) dao_type_string;
	args[1] = (DaoValue*) stream;
	meth = DaoType_FindFunctionChars( type, "(string)" );
	if( meth ){
		ec = DaoProcess_Call( proc, meth, self, args, 2 );
		if( ec ) ec = DaoProcess_Call( proc, meth, self, args, 1 );
	}else{
		meth = DaoType_FindFunctionChars( type, "serialize" );
		if( meth ) ec = DaoProcess_Call( proc, meth, self, NULL, 0 );
	}
	if( ec ){
		DaoProcess_RaiseException( proc, daoExceptionNames[ec], proc->string->chars, NULL );
	}else if( meth && proc->stackValues[0] ){
		DaoValue_Print( proc->stackValues[0], stream, cycmap, proc );
	}else{
		DaoStream_WriteString( stream, type->name );
		DaoStream_WriteChars( stream, buf );
	}
	if( inmap == NULL ) DMap_Delete( cycmap );
}

static DaoTypeCore daoCstructCore =
{
	"cstruct",                                             /* name */
	{ NULL },                                              /* bases */
	NULL,                                                  /* numbers */
	NULL,                                                  /* methods */
	DaoCstruct_CheckGetField,    DaoCstruct_DoGetField,    /* GetField */
	DaoCstruct_CheckSetField,    DaoCstruct_DoSetField,    /* SetField */
	DaoCstruct_CheckGetItem,     DaoCstruct_DoGetItem,     /* GetItem */
	DaoCstruct_CheckSetItem,     DaoCstruct_DoSetItem,     /* SetItem */
	DaoCstruct_CheckUnary,       DaoCstruct_DoUnary,       /* Unary */
	DaoCstruct_CheckBinary,      DaoCstruct_DoBinary,      /* Binary */
	NULL,                        NULL,                     /* Comparison */
	DaoCstruct_CheckConversion,  DaoCstruct_DoConversion,  /* Conversion */
	NULL,                        NULL,                     /* ForEach */
	DaoCstruct_Print,                                      /* Print */
	NULL,                                                  /* Slice */
	NULL,                                                  /* Copy */
	NULL,                                                  /* Delete */
	NULL                                                   /* HandleGC */
};

DaoTypeCore* DaoCstruct_GetDefaultCore()
{
	return & daoCstructCore;
}





DaoCdata* DaoCdata_New( DaoType *type, void *data )
{
	DaoCdata *self = (DaoCdata*)dao_calloc( 1, sizeof(DaoCdata) );
	DaoCstruct_Init( (DaoCstruct*)self, type );
	self->subtype = DAO_CDATA_CXX;
	self->data = data;
#ifdef DAO_USE_GC_LOGGER
	if( type == NULL ) DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

DaoCdata* DaoCdata_Wrap( DaoType *type, void *data )
{
	DaoCdata *self = DaoCdata_New( type, data );
	self->subtype = DAO_CDATA_PTR;
	return self;
}

static void DaoCdata_CoreDelete( DaoValue *self )
{
	DaoTypeCore *core = self->ctype->core;
	if( core->Delete != NULL && core->Delete != DaoCdata_CoreDelete ){
		core->Delete( self );
		return;
	}
	DaoCstruct_Free( (DaoCstruct*)self );
	dao_free( self );
}

void DaoCdata_Delete( DaoCdata *self )
{
	DaoCdata_CoreDelete( (DaoValue*) self );
}

int DaoCdata_IsType( DaoCdata *self, DaoType *type )
{
	return DaoType_ChildOf( self->ctype, type );
}

int DaoCdata_OwnData( DaoCdata *self )
{
	return self->subtype == DAO_CDATA_CXX;
}

void DaoCdata_SetType( DaoCdata *self, DaoType *type )
{
	if( type == NULL ) return;
	GC_Assign( & self->ctype, type );
}

void DaoCdata_SetData( DaoCdata *self, void *data )
{
	self->data = data;
}

void* DaoCdata_GetData( DaoCdata *self )
{
	return self->data;
}

void** DaoCdata_GetData2( DaoCdata *self )
{
	return & self->data;
}

DaoObject* DaoCdata_GetObject( DaoCdata *self )
{
	return (DaoObject*)self->object;
}

/*
// The "totype" should be a base or derived type of this cdata type:
*/
void* DaoCdata_CastData( DaoCdata *self, DaoType *totype )
{
	DaoValue *value;

	if( self == NULL || self->ctype == NULL || self->data == NULL ) return self->data;
	if( self->ctype->core->DoConversion == NULL ) return self->data;

	value = self->ctype->core->DoConversion( (DaoValue*) self, totype, NULL );
	if( value == NULL || value->type != DAO_CDATA ) return NULL;
	return value->xCdata.data;
}


DaoTypeCore daoCdataCore =
{
	"cdata",              /* name */
	{ NULL },             /* bases */
	NULL,                 /* numbers */
	NULL,                 /* methods */
	NULL,  NULL,          /* GetField */
	NULL,  NULL,          /* SetField */
	NULL,  NULL,          /* GetItem */
	NULL,  NULL,          /* SetItem */
	NULL,  NULL,          /* Unary */
	NULL,  NULL,          /* Binary */
	NULL,  NULL,          /* Comparison */
	NULL,  NULL,          /* Conversion */
	NULL,  NULL,          /* ForEach */
	DaoCdata_Print,       /* Print */
	NULL,                 /* Slice */
	NULL,                 /* Copy */
	DaoCdata_CoreDelete,  /* Delete */
	NULL                  /* HandleGC */
};





static DaoTypeCore daoCtypeCore;

DaoCtype* DaoCtype_New( DaoTypeCore *core, int tid )
{
	DaoCtype *self = (DaoCtype*)dao_calloc( 1, sizeof(DaoCtype) );
	DaoValue_Init( (DaoValue*)self, DAO_CTYPE );
	self->trait |= DAO_VALUE_NOCOPY;
	self->info = DString_New();
	self->name = DString_NewChars( core->name );
	self->classType = DaoType_New( core->name, DAO_CTYPE, (DaoValue*) self, NULL );
	self->objectTyppe = DaoType_New( core->name, tid, (DaoValue*) self, NULL );
	self->classType->core = & daoCtypeCore;
	self->objectType->core = core;
	GC_IncRC( self->classType );
	GC_IncRC( self->objectType );
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

void DaoCtype_Delete( DaoCtype *self )
{
	DString_Delete( self->name );
	DString_Delete( self->info );
	GC_DecRC( self->classType );
	GC_DecRC( self->objectType );
	dao_free( self );
}




DaoType* DaoCtype_CheckGetField( DaoType *self, DString *name, DaoRoutine *ctx )
{
	DaoValue *value = DaoType_FindValue( self->aux->xCtype.cdtype, name );
	DaoType *res = NULL;

	if( value && value->type == DAO_ROUTINE ){
		res = value->xRoutine.routType;
	}else if( value ){
		res = DaoNamespace_GetType( ctx->nameSpace, value );
	}
	return res;
}

DaoValue* DaoCtype_DoGetField( DaoValue *self, DString *name, DaoProcess *proc )
{
	DaoType *type = self->xCtype.cdtype;
	return DaoType_FindValue( type, name );
}


static void DaoCtype_CoreDelete( DaoValue *self )
{
	DaoCtype_Delete( (DaoCtype*) self );
}


static DaoTypeCore daoCtypeCore =
{
	"ctype",                                       /* name */
	{ NULL },                                      /* bases */
	NULL,                                          /* numbers */
	NULL,                                          /* methods */
	DaoCtype_CheckGetField,  DaoCtype_DoGetField,  /* GetField */
	NULL,                    NULL,                 /* GetField */
	NULL,                    NULL,                 /* SetField */
	NULL,                    NULL,                 /* GetItem */
	NULL,                    NULL,                 /* SetItem */
	NULL,                    NULL,                 /* Unary */
	NULL,                    NULL,                 /* Binary */
	NULL,                    NULL,                 /* Comparison */
	NULL,                    NULL,                 /* Conversion */
	NULL,                    NULL,                 /* ForEach */
	DaoCtype_Print,                                /* Print */
	NULL,                                          /* Slice */
	NULL,                                          /* Copy */
	DaoCtype_CoreDelete,                           /* Delete */
	NULL                                           /* HandleGC */
};





DaoException* DaoException_New( DaoType *type )
{
	DaoException *self = (DaoException*) dao_malloc( sizeof(DaoException) );
	DaoCstruct_Init( (DaoCstruct*)self, type );
	self->callers = DList_New( DAO_DATA_VALUE );
	self->lines = DList_New(0);
	self->info = DString_New();
	self->data = NULL;
	return self;
}

void DaoException_Delete( DaoException *self )
{
	DaoCstruct_Free( (DaoCstruct*)self );
	GC_DecRC( self->data );
	DString_Delete( self->info );
	DList_Delete( self->callers );
	DList_Delete( self->lines );
	dao_free( self );
}

void DaoException_SetData( DaoException *self, DaoValue *data )
{
	DaoValue_Move( data, & self->data, NULL );
}

void DaoException_Init( DaoException *self, DaoProcess *proc, const char *summary, DaoValue *dat )
{
	DaoVmCode *vmc = proc->activeCode;
	DaoRoutine *rout = proc->activeRoutine;
	DaoStackFrame *frame = proc->topFrame->prev;
	int line, id = (int) (vmc - proc->topFrame->active->codes);

	if( rout == NULL ) return;

	line = rout->defLine;
	if( proc->topFrame->active == proc->topFrame->prev ){
		DaoRoutine *rout2 = proc->topFrame->prev->routine;
		/*
		// proc->activeCode could be a dummy code set by:
		//   DaoProcess_InterceptReturnValue();
		// So always use the entry index whenever possible.
		*/
		id = proc->topFrame->prev->entry;
		if( rout2->body && id && id <= rout2->body->vmCodes->size ){
			line = rout2->body->annotCodes->items.pVmc[id-1]->line;
		}
	}else{
		id = (int) (vmc - proc->topFrame->active->codes);
		if( id < 0 || id > 0xffff ) id = 0; /* Not the precise location, but a safe one; */
		if( vmc && id < rout->body->vmCodes->size ){
			line = rout->body->annotCodes->items.pVmc[id]->line;
		}
	}

	if( summary && summary[0] != 0 ) DString_SetChars( self->info, summary );
	GC_Assign( & self->data, dat );

	DList_Clear( self->callers );
	DList_Clear( self->lines );
	DList_Append( self->callers, proc->topFrame->routine );
	DList_Append( self->lines, (daoint) (line<<16)|id );
	while( frame && frame != proc->startFrame->prev  && frame->routine ){
		DaoRoutineBody *body = frame->routine->body;
		if( self->callers->size >= 5 ) break;
		if( frame->entry ){
			/* deferred anonymous function may have been pushed but not executed: */
			line = body ? body->annotCodes->items.pVmc[ frame->entry - 1 ]->line : 0;
			DList_Append( self->callers, frame->routine );
			DList_Append( self->lines, (daoint) (line<<16)|(frame->entry - 1) );
		}
		frame = frame->prev;
	}
}
static void DaoType_WriteMainName( DaoType *self, DaoStream *stream )
{
	DString *name = self->name;
	daoint i, n = DString_FindChar( name, '<', 0 );
	if( n == DAO_NULLPOS ) n = name->size;
	for(i=0; i<n; i++) DaoStream_WriteChar( stream, name->chars[i] );
}
static void DString_Format( DString *self, int width, int head )
{
	daoint i, j, n, k = width - head;
	char buffer[32];
	if( head >= 30 ) head = 30;
	if( width <= head ) return;
	memset( buffer, ' ', head+1 );
	buffer[0] = '\n';
	buffer[head+1] = '\0';
	for(i=0,k=0; i<self->size; ++i,++k){
		if( k >= width ){
			DString_InsertChars( self, buffer, i, 0, head+1 );
			k = 0;
		}else if( self->chars[i] == '\n' ){
			k = 0;
		}
	}
}

static void DaoException_PrintName( DaoValue *exception, DaoStream *ss )
{
	if( exception == NULL || exception->type != DAO_OBJECT ) return;
	if( exception->xObject.parent ) DaoException_PrintName( exception->xObject.parent, ss );
	DaoStream_WriteChars( ss, "::" );
	DaoStream_WriteString( ss, exception->xObject.defClass->className );
}

void DaoException_Print( DaoException *self, DaoStream *stream )
{
	int codeShown = 0;
	int i, h, w = 100, n = self->callers->size;
	DaoStream *ss = DaoStream_New();
	DString *sstring;

	DaoStream_SetStringMode( ss );
	sstring = ss->buffer;
	DaoStream_WriteChars( ss, "[[" );
	DaoStream_WriteChars( ss, self->ctype->typer->name );
	DaoException_PrintName( (DaoValue*) self->object, ss );
	DaoStream_WriteChars( ss, "]] --- " );
	h = sstring->size;
	if( h > 40 ) h = 40;
	DaoStream_WriteString( ss, self->ctype->aux->xCtype.info );
	DaoStream_WriteChars( ss, ":\n" );
	DaoStream_WriteString( stream, sstring );
	DString_Clear( sstring );
	DaoStream_WriteString( ss, self->info );
	DString_Chop( sstring, 1 );
	DString_Format( sstring, w, h );
	DaoStream_WriteChars( ss, "\n" );
	DaoStream_WriteString( stream, sstring );
	DString_Clear( sstring );

	for(i=0; i<n; i++){
		DaoRoutine *rout = self->callers->items.pRoutine[i];
		if( codeShown == 0 && rout->subtype == DAO_ROUTINE ){
			DaoRoutine_PrintCodeSnippet( rout, stream, self->lines->items.pInt[i] & 0xffff );
			codeShown = 1;
		}
		DaoStream_WriteChars( ss, i == 0 ? "Raised by:  " : "Called by:  " );
		if( rout->attribs & DAO_ROUT_PARSELF ){
			DaoType *type = rout->routType->nested->items.pType[0];
			type = DaoType_GetBaseType( (DaoType*) type->aux );
			DaoType_WriteMainName( type, ss );
			DaoStream_WriteChars( ss, "." );
		}else if( rout->routHost ){
			DaoType_WriteMainName( rout->routHost, ss );
			DaoStream_WriteChars( ss, "." );
		}
		DaoStream_WriteString( ss, rout->routName );
		DaoStream_WriteChars( ss, "()," );
		if( rout->subtype == DAO_ROUTINE ){
			DaoStream_WriteChars( ss, " at instruction " );
			DaoStream_WriteInt( ss, self->lines->items.pInt[i] & 0xffff );
			DaoStream_WriteChars( ss, " in line " );
			DaoStream_WriteInt( ss, self->lines->items.pInt[i] >> 16 );
			DaoStream_WriteChars( ss, " in file \"" );
		}else{
			DaoStream_WriteChars( ss, " from namespace \"" );
		}
		DaoStream_WriteString( ss, rout->nameSpace->name );
		DaoStream_WriteChars( ss, "\";\n" );
		DString_Format( sstring, w, 12 );
		DaoStream_WriteString( stream, sstring );
		DString_Clear( sstring );
	}
	DaoStream_Delete( ss );
}

static void DaoException_CorePrint( DaoValue *self, DaoStream *stream, DMap *cycmap, DaoProcess *proc )
{
	DaoException_Print( (DaoException*) self, stream );
}

static void DaoException_CoreDelete( DaoValue *self )
{
	DaoException_Delete( (DaoException*) self );
}

static void DaoException_HandleGC( DaoValue *p, DList *values, DList *arrays, DList *maps, int remove )
{
	DaoException *self = (DaoException*) p;
	if( self->data ) DList_Append( values, self->data );
	if( self->callers->size ) DList_Append( arrays, self->callers );
	if( remove ) self->data = NULL;
}


static void Dao_Exception_New( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoType *type = proc->topFrame->routine->routHost;
	DaoException *self = (DaoException*)DaoException_New( type );
	if( n ) DString_Assign( self->info, p[0]->xString.value );
	DaoProcess_PutValue( proc, (DaoValue*)self );
}
static void Dao_Exception_New22( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoType *type = proc->topFrame->routine->routHost;
	DaoException *self = (DaoException*)DaoException_New( type );
	DaoException_SetData( self, p[0] );
	DaoProcess_PutValue( proc, (DaoValue*)self );
}

static void Dao_Exception_Get_name( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoProcess_PutChars( proc, self->ctype->typer->name );
}

static void Dao_Exception_Get_summary( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoProcess_PutString( proc, self->info );
}

static void Dao_Exception_Set_summary( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DString_Assign( self->info, p[1]->xString.value );
}

static void Dao_Exception_Get_data( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoProcess_PutValue( proc, self->data ? self->data : dao_none_value );
}

static void Dao_Exception_Set_data( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoValue_Move( p[1], & self->data, NULL );
}

static void Dao_Exception_Get_line( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoProcess_PutInteger( proc, self->lines->size ? (self->lines->items.pInt[0]>>16)&0xffff : 0 );
}

static void Dao_Exception_Serialize( DaoProcess *proc, DaoValue *p[], int n )
{
	DaoException* self = (DaoException*) p[0];
	DaoStream *stream = DaoStream_New();
	DaoStream_SetStringMode( stream );
	DaoException_Print( self, stream );
	DaoProcess_PutString( proc, stream->buffer );
	DaoGC_TryDelete( (DaoValue*) stream );
}

static void Dao_Exception_Define( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoType *etype;
	DString *host = proc->topFrame->routine->routHost->name;
	DString *name = p[0]->xString.value;
	DString *info = p[1]->xString.value;
	if( DString_Find( name, host, 0 ) != 0 && DString_FindChar( name, ':', 0 ) != host->size ){
		DString_InsertChars( name, "::", 0, 0, -1 );
		DString_Insert( name, host, 0, 0, -1 );
	}
	etype = DaoVmSpace_MakeExceptionType( proc->vmSpace, name->chars );
	if( etype == 0 ){
		DaoProcess_RaiseError( proc, "Param", "Invalid exception name" );
		return;
	}
	if( info->size ) DString_Assign( etype->aux->xCtype.info, info );
	DaoProcess_PutValue( proc, (DaoValue*) etype->aux );
}


const DaoFuncionEntry daoExceptionMeths[] =
{
	{ Dao_Exception_Get_name,    ".name( self: Exception )=>string" },
	{ Dao_Exception_Get_summary, ".summary( self: Exception )=>string" },
	{ Dao_Exception_Set_summary, ".summary=( self: Exception, summary: string)" },
	{ Dao_Exception_Get_data,    ".data( self: Exception )=>any" },
	{ Dao_Exception_Set_data,    ".data=( self: Exception, data: any)" },
	{ Dao_Exception_Get_line,    ".line( self: Exception )=>int" },

	/* for testing or demonstration */
	{ Dao_Exception_Get_name,    "typename( self: Exception )=>string" },
	{ Dao_Exception_Serialize,   "serialize( self: Exception )=>string" },
	{ Dao_Exception_Serialize,   "(string)( self: Exception )" },

	{ NULL, NULL }
};

const DaoTypeCore daoExceptionCore =
{
	"Exception",                                       /* name */
	{ NULL },                                          /* bases */
	NULL,                                              /* numbers */
	daoExceptionMeths,                                 /* methods */
	DaoCstruct_CheckGetField,  DaoCstruct_DoGetField,  /* GetField */
	DaoCstruct_CheckSetField,  DaoCstruct_DoSetField,  /* SetField */
	DaoCstruct_CheckGetItem,   DaoCstruct_DoGetItem,   /* GetItem */
	DaoCstruct_CheckSetItem,   DaoCstruct_DoSetItem,   /* SetItem */
	NULL,                      NULL,                   /* Unary */
	NULL,                      NULL,                   /* Binary */
	NULL,                      NULL,                   /* Comparison */
	NULL,                      NULL,                   /* Conversion */
	NULL,                      NULL,                   /* ForEach */
	DaoException_CorePrint,                            /* Print */
	NULL,                                              /* Slice */
	NULL,                                              /* Copy */
	DaoException_CoreDelete,                           /* Delete */
	DaoException_HandleGC                              /* HandleGC */
};


const DaoFunctionEntry daoExceptionWarningMeths[] =
{
	{ Dao_Exception_New, "Warning( summary = \"\" )" },
	{ Dao_Exception_Define, "define( name: string, info = '' ) => class<Warning>" },
	{ NULL, NULL }
};


const DaoTypeCore daoExceptionWarningCore =
{
	"Exception::Warning",                              /* name */
	{ & daoExceptionCore, NULL },                      /* bases */
	NULL,                                              /* numbers */
	daoExceptionWarningMeths,                          /* methods */
	DaoCstruct_CheckGetField,  DaoCstruct_DoGetField,  /* GetField */
	DaoCstruct_CheckSetField,  DaoCstruct_DoSetField,  /* SetField */
	DaoCstruct_CheckGetItem,   DaoCstruct_DoGetItem,   /* GetItem */
	DaoCstruct_CheckSetItem,   DaoCstruct_DoSetItem,   /* SetItem */
	NULL,                      NULL,                   /* Unary */
	NULL,                      NULL,                   /* Binary */
	NULL,                      NULL,                   /* Comparison */
	NULL,                      NULL,                   /* Conversion */
	NULL,                      NULL,                   /* ForEach */
	DaoException_CorePrint,                            /* Print */
	NULL,                                              /* Slice */
	NULL,                                              /* Copy */
	DaoException_CoreDelete,                           /* Delete */
	DaoException_HandleGC                              /* HandleGC */
};


const DaoFunctionEntry daoExceptionErrorMeths[] =
{
	{ Dao_Exception_New, "Error( summary = \"\" )" },
	{ Dao_Exception_New22, "Error( data: any )" },
	{ Dao_Exception_Define, "define( name: string, info = '' ) => class<Error>" },
	{ NULL, NULL }
};


const DaoTypeCore daoExceptionErrorCore =
{
	"Exception::Error",                                /* name */
	{ & daoExceptionCore, NULL },                      /* bases */
	NULL,                                              /* numbers */
	daoExceptionErrorMeths,                            /* methods */
	DaoCstruct_CheckGetField,  DaoCstruct_DoGetField,  /* GetField */
	DaoCstruct_CheckSetField,  DaoCstruct_DoSetField,  /* SetField */
	DaoCstruct_CheckGetItem,   DaoCstruct_DoGetItem,   /* GetItem */
	DaoCstruct_CheckSetItem,   DaoCstruct_DoSetItem,   /* SetItem */
	NULL,                      NULL,                   /* Unary */
	NULL,                      NULL,                   /* Binary */
	NULL,                      NULL,                   /* Comparison */
	NULL,                      NULL,                   /* Conversion */
	NULL,                      NULL,                   /* ForEach */
	DaoException_CorePrint,                            /* Print */
	NULL,                                              /* Slice */
	NULL,                                              /* Copy */
	DaoException_CoreDelete,                           /* Delete */
	DaoException_HandleGC                              /* HandleGC */
};


void DaoException_Setup( DaoNamespace *ns )
{
	dao_type_exception = DaoNamespace_WrapType( ns, & daoExceptionCore, DAO_CSTRUCT, 0 );
	dao_type_warning = DaoNamespace_WrapType( ns, & daoExceptionWarningCore, DAO_CSTRUCT, 0 );
	dao_type_error = DaoNamespace_WrapType( ns, & daoExceptionErrorCore, DAO_CSTRUCT, 0 );
	DaoNamespace_AddType( ns, dao_type_warning->name, dao_type_warning );
	DaoNamespace_AddType( ns, dao_type_error->name, dao_type_error );
	DaoNamespace_AddTypeConstant( ns, dao_type_warning->name, dao_type_warning );
	DaoNamespace_AddTypeConstant( ns, dao_type_error->name, dao_type_error );
	DString_SetChars( dao_type_exception->aux->xCtype.info, daoExceptionTitles[0] );
	DString_SetChars( dao_type_warning->aux->xCtype.info, daoExceptionTitles[1] );
	DString_SetChars( dao_type_error->aux->xCtype.info, daoExceptionTitles[2] );
}





DaoConstant* DaoConstant_New( DaoValue *value, int subtype )
{
	DaoConstant *self = (DaoConstant*) dao_calloc( 1, sizeof(DaoConstant) );
	DaoValue_Init( self, DAO_CONSTANT );
	DaoValue_Copy( value, & self->value );
	self->subtype = subtype;
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

DaoVariable* DaoVariable_New( DaoValue *value, DaoType *type, int subtype )
{
	DaoVariable *self = (DaoVariable*) dao_calloc( 1, sizeof(DaoVariable) );
	DaoValue_Init( self, DAO_VARIABLE );
	DaoValue_Move( value, & self->value, type );
	self->subtype = subtype;
	self->dtype = type;
	GC_IncRC( type );
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogNew( (DaoValue*) self );
#endif
	return self;
}

void DaoConstant_Delete( DaoConstant *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	GC_DecRC( self->value );
	dao_free( self );
}
void DaoVariable_Delete( DaoVariable *self )
{
#ifdef DAO_USE_GC_LOGGER
	DaoObjectLogger_LogDelete( (DaoValue*) self );
#endif
	GC_DecRC( self->value );
	GC_DecRC( self->dtype );
	dao_free( self );
}
void DaoConstant_Set( DaoConstant *self, DaoValue *value )
{
	DaoValue_Copy( value, & self->value );
}
int DaoVariable_Set( DaoVariable *self, DaoValue *value, DaoType *type )
{
	if( type ) GC_Assign( & self->dtype, type );
	return DaoValue_Move( value, & self->value, self->dtype );
}
void DaoVariable_SetType( DaoVariable *self, DaoType *type )
{
	GC_Assign( & self->dtype, type );
	if( self->value == NULL || self->value->type != type->value->type ){
		GC_DecRC( self->value );
		self->value = DaoValue_SimpleCopy( type->value );
		GC_IncRC( self->value );
	}
}


