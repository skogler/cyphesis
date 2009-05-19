// Cyphesis Online RPG Server and AI Engine
// Copyright (C) 2009 Alistair Riddoch
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

// $Id$

#include <Python.h>

#include "rulesets/Python_API.h"

#include <cassert>

int main()
{
    init_python_api();

    assert(PyRun_SimpleString("from atlas import Message") == 0);
    assert(PyRun_SimpleString("from atlas import Operation") == 0);
    assert(PyRun_SimpleString("m=Message()") == 0);
    assert(PyRun_SimpleString("m.append(None)") == 0);
    assert(PyRun_SimpleString("m.append(1)") == -1);
    assert(PyRun_SimpleString("m.append(Operation('get'))") == 0);
    assert(PyRun_SimpleString("m.append(Message())") == 0);
    assert(PyRun_SimpleString("m.append(Message(Operation('get')))") == 0);
    assert(PyRun_SimpleString("m += None") == 0);
    assert(PyRun_SimpleString("m += 1") == -1);
    assert(PyRun_SimpleString("m += Operation('get')") == 0);
    assert(PyRun_SimpleString("m += Message()") == 0);
    assert(PyRun_SimpleString("m += Message(Operation('get'))") == 0);
    assert(PyRun_SimpleString("len(m)") == 0);
    

    shutdown_python_api();
    return 0;
}
