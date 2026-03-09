#include "ProjectConfig.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;

static string trim( const string &s )
{
	size_t start = s.find_first_not_of( " \t\r\n" );
	if ( start == string::npos )
		return "";
	size_t end = s.find_last_not_of( " \t\r\n" );
	return s.substr( start, end - start + 1 );
}

static string unquote( const string &s )
{
	string t = trim( s );
	if ( t.size() >= 2 && t.front() == '"' && t.back() == '"' )
		return t.substr( 1, t.size() - 2 );
	return t;
}

static void parseInlineTable( const string &name, const string &body, vector<Dependency> &deps )
{
	Dependency dep;
	dep.name = name;

	// Split on commas, parse each key = "value" pair
	string content = body;
	while ( !content.empty() )
	{
		size_t comma = string::npos;
		bool inQuote = false;
		for ( size_t i = 0; i < content.size(); i++ )
		{
			if ( content[i] == '"' )
				inQuote = !inQuote;
			else if ( content[i] == ',' && !inQuote )
			{
				comma = i;
				break;
			}
		}

		string pair;
		if ( comma != string::npos )
		{
			pair = content.substr( 0, comma );
			content = content.substr( comma + 1 );
		}
		else
		{
			pair = content;
			content.clear();
		}

		size_t eq = pair.find( '=' );
		if ( eq == string::npos )
			continue;

		string key = trim( pair.substr( 0, eq ) );
		string value = unquote( pair.substr( eq + 1 ) );

		if ( key == "path" )
			dep.path = value;
		else if ( key == "git" )
			dep.gitUrl = value;
		else if ( key == "tag" )
			dep.tag = value;
	}

	deps.push_back( dep );
}

ProjectConfig *ProjectConfig::loadFromFile( const string &path )
{
	ifstream file( path );
	if ( !file.is_open() )
		return nullptr;

	ProjectConfig *config = new ProjectConfig();
	string currentSection;
	string line;
	int lineNum = 0;

	while ( getline( file, line ) )
	{
		lineNum++;
		// Strip comments
		{
			bool inQuote = false;
			for ( size_t i = 0; i < line.size(); i++ )
			{
				if ( line[i] == '"' )
					inQuote = !inQuote;
				else if ( line[i] == '#' && !inQuote )
				{
					line = line.substr( 0, i );
					break;
				}
			}
		}

		line = trim( line );
		if ( line.empty() )
			continue;

		// Section header
		if ( line.front() == '[' && line.back() == ']' )
		{
			currentSection = trim( line.substr( 1, line.size() - 2 ) );
			continue;
		}

		// Key = value
		size_t eq = line.find( '=' );
		if ( eq == string::npos )
		{
			cerr << path << ":" << lineNum << ": parse error: expected key = value" << endl;
			continue;
		}

		string key = trim( line.substr( 0, eq ) );
		string value = trim( line.substr( eq + 1 ) );

		if ( currentSection == "project" )
		{
			string val = unquote( value );
			if ( key == "name" )
				config->mName = val;
			else if ( key == "version" )
				config->mVersion = val;
			else if ( key == "type" )
				config->mType = val;
		}
		else if ( currentSection == "deps" )
		{
			// Expect inline table: name = { key = "val", ... }
			size_t braceOpen = value.find( '{' );
			size_t braceClose = value.rfind( '}' );
			if ( braceOpen != string::npos && braceClose != string::npos && braceClose > braceOpen )
			{
				string body = value.substr( braceOpen + 1, braceClose - braceOpen - 1 );
				parseInlineTable( key, body, config->mDeps );
			}
			else
			{
				cerr << path << ":" << lineNum << ": parse error: expected inline table for dependency '" << key << "'" << endl;
			}
		}
	}

	return config;
}

ProjectConfig *ProjectConfig::loadFromDirectory( const string &dir )
{
	string path = dir;
	if ( !path.empty() && path.back() != '/' )
		path += '/';
	path += "blang.toml";
	return loadFromFile( path );
}
