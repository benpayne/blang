#include "BmodEmitter.h"
#include "Expression.h"

#include <iostream>

using namespace QLang;
using namespace std;

// Helper to get a non-const Type* from various const SmartPtr contexts.
// The BmodEmitter only reads from types, never modifies them.
static Type *nc( const Type *t ) { return const_cast<Type*>( t ); }

void BmodEmitter::emitType( Type *type, ostream &out )
{
	out << type->getName();
	if ( type->getNumTypeParams() > 0 )
	{
		out << "<";
		for ( int i = 0; i < type->getNumTypeParams(); i++ )
		{
			if ( i > 0 )
				out << ", ";
			emitType( type->getTypeParam( i ), out );
		}
		out << ">";
	}
}

void BmodEmitter::emitAnnotations( const vector<AnnotationNode> &annotations, ostream &out )
{
	for ( const auto &ann : annotations )
	{
		out << "@" << ann.mName;
		if ( !ann.mArgs.empty() )
		{
			out << "(";
			for ( size_t i = 0; i < ann.mArgs.size(); i++ )
			{
				if ( i > 0 )
					out << ", ";
				out << "\"" << ann.mArgs[i] << "\"";
			}
			out << ")";
		}
		out << endl;
	}
}

void BmodEmitter::emitGenericParams( const vector<GenericParam> &params, ostream &out )
{
	if ( params.empty() )
		return;

	out << "<";
	for ( size_t i = 0; i < params.size(); i++ )
	{
		if ( i > 0 )
			out << ", ";
		out << params[i].mName;
		if ( !params[i].mConstraint.empty() )
			out << ": " << params[i].mConstraint;
	}
	out << ">";
}

void BmodEmitter::emitFunction( FunctionDefinition *func, ostream &out )
{
	if ( !func->isPublic() )
		return;

	emitAnnotations( func->getAnnotations(), out );

	out << "pub fn " << func->getName();
	emitGenericParams( func->getGenericParams(), out );
	out << "(";

	for ( int i = 0; i < func->getNumberParams(); i++ )
	{
		if ( i > 0 )
			out << ", ";
		VariableDefinition *param = func->getParam( i );
		emitType( nc( param->getVariableType() ), out );
		out << " " << param->getName();
	}

	if ( func->isVariadic() )
	{
		if ( func->getNumberParams() > 0 )
			out << ", ";
		out << "...";
	}

	out << ")";

	if ( func->getReturnType() != nullptr )
	{
		out << " -> ";
		emitType( func->getReturnType(), out );
	}

	out << ";" << endl;
}

void BmodEmitter::emitStruct( StructDefinition *structDef, ostream &out )
{
	if ( !structDef->isPublic() )
		return;

	emitAnnotations( structDef->getAnnotations(), out );

	if ( structDef->isTable() )
		out << "table ";
	out << "pub struct " << structDef->getName();
	emitGenericParams( structDef->getGenericParams(), out );
	out << " {" << endl;

	for ( const auto &field : structDef->getFields() )
	{
		out << "\t";
		emitType( nc( field->getVariableType() ), out );
		out << " " << field->getName() << ";" << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emitEnum( EnumDefinition *enumDef, ostream &out )
{
	if ( !enumDef->isPublic() )
		return;

	emitAnnotations( enumDef->getAnnotations(), out );

	out << "pub enum " << enumDef->getName();
	emitGenericParams( enumDef->getGenericParams(), out );
	out << " {" << endl;

	const auto &variants = enumDef->getVariants();
	for ( size_t i = 0; i < variants.size(); i++ )
	{
		out << "\t" << variants[i].mName;
		if ( !variants[i].mAssociatedTypes.empty() )
		{
			out << "(";
			for ( size_t j = 0; j < variants[i].mAssociatedTypes.size(); j++ )
			{
				if ( j > 0 )
					out << ", ";
				emitType( nc( (const Type*)variants[i].mAssociatedTypes[j] ), out );
			}
			out << ")";
		}
		if ( i + 1 < variants.size() )
			out << ",";
		out << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emitProtocol( ProtocolDefinition *protoDef, ostream &out )
{
	if ( !protoDef->isPublic() )
		return;

	out << "pub protocol " << protoDef->getName();
	emitGenericParams( protoDef->getGenericParams(), out );
	out << " {" << endl;

	for ( const auto &sp : protoDef->getRequiredMethods() )
	{
		FunctionDefinition *method = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
		out << "\tfn " << method->getName() << "(";

		for ( int i = 0; i < method->getNumberParams(); i++ )
		{
			if ( i > 0 )
				out << ", ";
			VariableDefinition *param = method->getParam( i );
			if ( param->getVariableType()->getName() == "self" )
				out << "self";
			else
			{
				emitType( nc( param->getVariableType() ), out );
				out << " " << param->getName();
			}
		}

		out << ")";

		if ( method->getReturnType() != nullptr )
		{
			out << " -> ";
			emitType( method->getReturnType(), out );
		}

		out << ";" << endl;
	}

	out << "}" << endl;
}

void BmodEmitter::emit( const vector<Module*> &modules, ostream &out )
{
	out << "// auto-generated .bmod interface file — do not edit" << endl;
	out << endl;

	for ( auto *mod : modules )
	{
		// Emit structs
		for ( const auto &sp : mod->getStructList() )
		{
			StructDefinition *structDef = const_cast<StructDefinition*>( (const StructDefinition*)sp );
			emitStruct( structDef, out );
			out << endl;
		}

		// Emit enums
		for ( const auto &sp : mod->getEnumList() )
		{
			EnumDefinition *enumDef = const_cast<EnumDefinition*>( (const EnumDefinition*)sp );
			emitEnum( enumDef, out );
			out << endl;
		}

		// Emit protocols
		for ( const auto &sp : mod->getProtocolList() )
		{
			ProtocolDefinition *protoDef = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
			emitProtocol( protoDef, out );
			out << endl;
		}

		// Emit functions
		for ( const auto &sp : mod->getFunctionList() )
		{
			FunctionDefinition *func = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
			emitFunction( func, out );
		}
	}
}
