// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2000 Alistair Riddoch

#include <Atlas/Message/Object.h>
#include <Atlas/Objects/Root.h>
#include <Atlas/Objects/Operation/Login.h>

#include "OOG_Thing.h"

oplist OOGThing::Operation(const RootOperation & op) {
    return(error(op, "Unknown operation"));
}

