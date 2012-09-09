/*
// Dao Virtual Machine
// http://www.daovm.net
//
// Copyright (c) 2006-2012, Limin Fu
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
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DAO_BYTECODE_H
#define DAO_BYTECODE_H

#include "daoStdtype.h"

typedef struct DaoByteEncoder  DaoByteEncoder;
typedef struct DaoByteDecoder  DaoByteDecoder;

struct DaoByteEncoder
{
	DaoNamespace  *nspace;

	daoint    valueCount;

	DString  *header;
	DString  *source;
	DString  *modules;
	DString  *identifiers;
	DString  *scobjects;
	DString  *types;
	DString  *values;
	DString  *constants;
	DString  *variables;
	DString  *interfaces;
	DString  *classes;
	DString  *routines;

	DString  *valueBytes;
	DArray   *lookups;      /* <daoint> */
	DArray   *lookups2;     /* <daoint> */
	DArray   *names;        /* <DString*> (not managed); */
	DArray   *names2;       /* <DString*> (not managed); */

	DMap  *mapIdentifiers;  /* <DString*,daoint> */
	DMap  *mapScobjects;    /* <DaoValue*,daoint> */
	DMap  *mapTypes;        /* <DaoType*,daoint> */
	DMap  *mapValues;       /* <DaoValue*,daoint> */
	DMap  *mapValueBytes;   /* <DString*,daoint> */
	DMap  *mapInterfaces;   /* <DaoInterface*,daoint> */
	DMap  *mapClasses;      /* <DaoClass*,daoint> */
	DMap  *mapRoutines;     /* <DaoRoutine*,daoint> */
};

DaoByteEncoder* DaoByteEncoder_New();
void DaoByteEncoder_Delete( DaoByteEncoder *self );
void DaoByteEncoder_Reset( DaoByteEncoder *self );

void DaoByteEncoder_Encode( DaoByteEncoder *self, DaoNamespace *nspace, DString *output );



struct DaoByteDecoder
{
	DaoVmSpace  *vmspace;

	DArray   *identifiers;  /* <DString*> */
	DArray   *scobjects;    /* <DaoValue*> */
	DArray   *types;        /* <DaoType*> */
	DArray   *values;       /* <DString*>: encoded values; */
	DArray   *interfaces;   /* <DaoInterface*> */
	DArray   *classes;      /* <DaoClass*> */
	DArray   *routines;     /* <DaoRoutine*> */
};


DaoByteDecoder* DaoByteDecoder_New( DaoVmSpace *vmspace );
void DaoByteDecoder_Delete( DaoByteDecoder *self );
void DaoByteDecoder_Reset( DaoByteDecoder *self );

DaoNamespace* DaoByteDecoder_Decode( DaoByteDecoder *self, DString *input );

#endif
