/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
 
#include "precompiled.h"
#include <Rocket/Core/Lua/Interpreter.h>
#include <Rocket/Core/Lua/Utilities.h>
#include <Rocket/Core/Log.h>
#include <Rocket/Core/String.h>
#include <Rocket/Core/FileInterface.h>
#include <Rocket/Core/Lua/LuaType.h>
#include "LuaDocumentElementInstancer.h"
#include <Rocket/Core/Factory.h>
#include <sstream>
#include "LuaEventListenerInstancer.h"
#include "Rocket.h"
//the types I made
#include "ContextDocumentsProxy.h"
#include "EventParametersProxy.h"
#include "ElementAttributesProxy.h"
#include "Log.h"
#include "Element.h"
#include "ElementStyleProxy.h"
#include "Document.h"
#include "Colourb.h"
#include "Colourf.h"
#include "Vector2f.h"
#include "Vector2i.h"
#include "Context.h"
#include "Event.h"
#include "ElementInstancer.h"
#include "ElementChildNodesProxy.h"
#include "ElementText.h"
#include "GlobalLuaFunctions.h"
#include "RocketContextsProxy.h"

namespace Rocket {
namespace Core {
namespace Lua {
lua_State* Interpreter::_Lua = NULL;
std::unique_ptr<LuaSystemInterface> Interpreter::_SystemInterface;
//typedefs for nicer Lua names
typedef Rocket::Core::ElementDocument Document;

void Interpreter::Startup()
{
	if(_Lua == NULL)
	{
		Log::Message(Log::LT_INFO, "Loading Lua interpreter");
		_Lua = luaL_newstate();
		luaL_openlibs(_Lua);
	}
    RegisterCoreTypes(_Lua);
}


void Interpreter::RegisterCoreTypes(lua_State* L)
{
    LuaType<Vector2i>::Register(L);
    LuaType<Vector2f>::Register(L);
    LuaType<Colourf>::Register(L);
    LuaType<Colourb>::Register(L);
    LuaType<Log>::Register(L);
    LuaType<ElementStyleProxy>::Register(L);
    LuaType<Element>::Register(L);
        //things that inherit from Element
        LuaType<Document>::Register(L);
        LuaType<ElementText>::Register(L);
    LuaType<Event>::Register(L);
    LuaType<Context>::Register(L);
    LuaType<LuaRocket>::Register(L);
    LuaType<ElementInstancer>::Register(L);
    //Proxy tables
    LuaType<ContextDocumentsProxy>::Register(L);
    LuaType<EventParametersProxy>::Register(L);
    LuaType<ElementAttributesProxy>::Register(L);
    LuaType<ElementChildNodesProxy>::Register(L);
    LuaType<RocketContextsProxy>::Register(L);
    OverrideLuaGlobalFunctions(L);
    //push the global variable "rocket" to use the "Rocket" methods
    LuaRocketPushrocketGlobal(L);
}



void Interpreter::LoadFile(const String& file, Rocket::Core::Element* context)
{
    //use the file interface to get the contents of the script
    Rocket::Core::FileInterface* file_interface = Rocket::Core::GetFileInterface();
    Rocket::Core::FileHandle handle = file_interface->Open(file);
    if(handle == 0) {
        lua_pushfstring(_Lua, "LoadFile: Unable to open file: %s", file.CString());
        Report(_Lua);
        return;
    }

    size_t size = file_interface->Length(handle);
    if(size == 0) {
        lua_pushfstring(_Lua, "LoadFile: File is 0 bytes in size: %s", file.CString());
        Report(_Lua);
        return;
    }
    char* file_contents = new char[size];
    file_interface->Read(file_contents,size,handle);
    file_interface->Close(handle);

    if(luaL_loadbuffer(_Lua,file_contents,size,file.CString()) != 0)
        Report(_Lua);
    else //if there were no errors loading, then the compiled function is on the top of the stack
    {
        Lua::Interpreter::GetLuaSystemInterface()->PrepareFunction(_Lua, -1, context);
        if(lua_pcall(_Lua,0,0,0) != 0)
            Report(_Lua);
    }

    delete[] file_contents;
}


void Interpreter::DoString(const Rocket::Core::String& code, Rocket::Core::Element* context, const Rocket::Core::String& name)
{
    if(luaL_loadbuffer(_Lua,code.CString(),code.Length(), name.CString()) != 0)
        Report(_Lua);
    else
    {
        Lua::Interpreter::GetLuaSystemInterface()->PrepareFunction(_Lua, -1, context);
        if(lua_pcall(_Lua,0,0,0) != 0)
            Report(_Lua);
    }
}

void Interpreter::LoadString(const Rocket::Core::String& code, Rocket::Core::Element* context, const Rocket::Core::String& name)
{
    if(luaL_loadbuffer(_Lua,code.CString(),code.Length(), name.CString()) != 0)
        Report(_Lua);
    Lua::Interpreter::GetLuaSystemInterface()->PrepareFunction(_Lua, -1, context);
}


void Interpreter::BeginCall(int funRef)
{
    lua_settop(_Lua,0); //empty stack
    //lua_getref(_Lua,funRef);
    lua_rawgeti(_Lua, LUA_REGISTRYINDEX, (int)funRef);
}

int Interpreter::ErrorHandler(lua_State* L) {
    if (_SystemInterface) {
        return _SystemInterface->ErrorHandler(L);
    }

	std::stringstream msgStream;

	msgStream << "LUA ERROR: " << lua_tostring(L, -1);
	lua_pop(L, -1);

	// Get the stack via the debug.traceback() function
	lua_getglobal(L, LUA_DBLIBNAME);

	if (!lua_isnil(L, -1))
	{
		msgStream << "\n";
		lua_getfield(L, -1, "traceback");
		lua_remove(L, -2);

		if (lua_pcall(L, 0, 1, 0) != 0)
			msgStream << "Error while retrieving stack: " << lua_tostring(L, -1);
		else
			msgStream << lua_tostring(L, -1);

		lua_pop(L, 1);
	}
	msgStream << "\n";

	auto message = msgStream.str();
	lua_pushstring(L, message.c_str());

	return 1;
}

bool Interpreter::ExecuteCall(int params, int res)
{
    bool ret = true;
    int top = lua_gettop(_Lua);
    if(lua_type(_Lua,top-params) != LUA_TFUNCTION)
    {
        ret = false;
        //stack cleanup
        if(params > 0)
        {
            for(int i = top; i >= (top-params); i--)
            {
                if(!lua_isnone(_Lua,i))
                    lua_remove(_Lua,i);
            }
        }
    }
    else
    {
        lua_pushcfunction(_Lua, ErrorHandler);
        lua_insert(_Lua, -params - 2);

        if(lua_pcall(_Lua,params,res,-params - 2) != 0)
        {
            Report(_Lua);
            ret = false;
            res = 0;
        }

        lua_remove(_Lua, -res - 1); // Remove error function from stack
    }
    return ret;
}

void Interpreter::EndCall(int res)
{
    //stack cleanup
    for(int i = res; i > 0; i--)
    {
        if(!lua_isnone(_Lua,res))
            lua_remove(_Lua,res);
    }
}

lua_State* Interpreter::GetLuaState() { return _Lua; }


//From Plugin
int Interpreter::GetEventClasses()
{
    return EVT_BASIC;
}

void Interpreter::OnInitialise()
{
    Startup();
    Factory::RegisterElementInstancer("body",new LuaDocumentElementInstancer())->RemoveReference();
    Factory::RegisterEventListenerInstancer(new LuaEventListenerInstancer())->RemoveReference();
}

void Interpreter::OnShutdown()
{
	// Commit suicide
	delete this;
}

void Interpreter::Initialise(std::unique_ptr<LuaSystemInterface> interface)
{
    Rocket::Core::Lua::Interpreter::Initialise(NULL, std::move(interface));
}

void Interpreter::Initialise(lua_State *luaStatePointer, std::unique_ptr<LuaSystemInterface> interface)
{
	Interpreter *iPtr = new Interpreter();
	_Lua = luaStatePointer;
	_SystemInterface = std::move(interface);
	Rocket::Core::RegisterPlugin(iPtr);
}

void Interpreter::Shutdown(bool free_state)
{
	_SystemInterface.reset();
	if (free_state) {
		lua_close(_Lua);
	}
}
LuaSystemInterface* Interpreter::GetLuaSystemInterface()
{
	return _SystemInterface.get();
}

}
}
}
