// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2000 Alistair Riddoch

#include <Atlas/Message/Object.h>
#include <Atlas/Net/Stream.h>
#include <Atlas/Objects/Root.h>
#include <Atlas/Objects/Encoder.h>
#include <Atlas/Objects/Decoder.h>
#include <Atlas/Objects/Operation/Create.h>
#include <Atlas/Objects/Operation/Login.h>
#include <Atlas/Objects/Operation/Info.h>
#include <Atlas/Objects/Operation/Get.h>

#include <common/Chop.h>
#include <common/Cut.h>
#include <common/Eat.h>
#include <common/Fire.h>

#include <rulesets/Character.h>
#include <rulesets/ExternalMind.h>
#include <common/debug.h>
#include <common/persistance.h>
#include <common/globals.h>

#include "Connection.h"
#include "ServerRouting.h"
#include "CommClient.h"
#include "CommServer.h"
#include "Player.h"

static const bool debug_flag = false;

using Atlas::Message::Object;
using Atlas::Objects::Operation::Info;

Connection::Connection(CommClient & client) : commClient(client),
    server(commClient.commServer.server) { }

void Connection::send(const RootOperation * msg) const {
    commClient.send(msg);
}

Account * Connection::addPlayer(string & username, string & password)
{
    Player * player=new Player(this, username, password);
    addObject(player);
    player->connection=this;
    player->world=server.world;
    server.addObject(player);
    return(player);
}

void Connection::destroy()
{
    debug(cout << "destroy called";);
    dict_t::const_iterator I;
    for(I = objects.begin(); I != objects.end(); I++) {
        BaseEntity * ent = I->second;
        if (ent->inGame == false) {
            continue;
        }
        Thing * obj = (Thing*)ent;
        if (obj->isCharacter == true) {
            Character * character = (Character *)obj;
            if (character->externalMind != NULL) {
                character->externalMind = NULL;
            }
        }
    }
    BaseEntity::destroy();
}

oplist Connection::operation(const RootOperation & op)
{
    debug(cout << "Connection::operation" << endl << flush;);
    const string & from = op.GetFrom();
    if (0==from.size()) {
        debug(cout << "deliver locally as normal" << endl << flush;);
        return BaseEntity::operation(op);
    } else {
        debug(cout << "Must send on to account" << endl << flush;);
        debug(cout << "[" << from << "]" << endl << flush;);
        if (objects.find(from)!=objects.end()) {
            BaseEntity * ent = objects[from];
            if ((ent->inGame != false)&&(((Thing *)ent)->isCharacter != 0) &&
                (((Character *)ent)->externalMind == NULL)) {
                Character * pchar = (Character *)ent;
                pchar->externalMind = new ExternalMind(*this, pchar->fullid, pchar ->name);
                cout << "Re-connecting existing character to new connection" << endl << flush;
                Info * info = new Info();
                *info = Info::Instantiate();
                Object::ListType args(1,pchar->asObject());
                info->SetArgs(args);
                info->SetRefno(op.GetSerialno());
                oplist res = ent->externalOperation(op);
                res.insert(res.begin(), info);
                return res;
            }
            return ent->externalOperation(op);
        } else {
            return error(op, "From is illegal");
        }
    }
    return oplist();
}

oplist Connection::Operation(const Login & op)
{

    debug(cout << "Got login op" << endl << flush;);
    const Object & account = op.GetArgs().front();

    if (account.IsMap()) {
        string account_id = account.AsMap().find("id")->second.AsString();
        string password = account.AsMap().find("password")->second.AsString();

        Account * player = (Player *)server.getObject(account_id);
        if (player == NULL) {
            player = Persistance::instance()->getAccount(account_id);
            if (player != NULL) {
                player->world=server.world;
                server.addObject(player);
            }
        }
        if (player && (account_id.size()!=0) && (password==player->password)) {
            addObject(player);
            dict_t::const_iterator I;
            for (I=player->charactersDict.begin();
                 I!=player->charactersDict.end(); I++) {
                addObject(I->second);
            }
            player->connection=this;
            Info * info = new Info();
            *info = Info::Instantiate();
            Object::ListType args(1,player->asObject());
            info->SetArgs(args);
            info->SetRefno(op.GetSerialno());
            debug(cout << "Good login" << endl << flush;);
            return(oplist(1,info));
        }
    }
    return error(op, "Login is invalid");
}

oplist Connection::Operation(const Create & op)
{
    debug(cout << "Got create op" << endl << flush;);
    const Object & account = op.GetArgs().front();

    if (Persistance::restricted) {
        return error(op, "Account creation on this server is restricted");
    }

    if (account.IsMap()) {
        string account_id = account.AsMap().find("id")->second.AsString();
        string password = account.AsMap().find("password")->second.AsString();

        if ((NULL==server.getObject(account_id)) && 
            (!Persistance::instance()->findAccount(account_id)) &&
            (account_id.size() != 0) && (password.size() != 0)) {
            Account * player = addPlayer(account_id, password);
            Persistance::instance()->putAccount(player);
            Info * info = new Info();
            *info = Info::Instantiate();
            Object::ListType args(1,player->asObject());
            info->SetArgs(args);
            info->SetRefno(op.GetSerialno());
            debug(cout << "Good create" << endl << flush;);
            return(oplist(1,info));
        }
    }
    return error(op, "Account creation is invalid");

}

oplist Connection::Operation(const Logout & op)
{
    const Object & account = op.GetArgs().front();
    
    if (account.IsMap()) {
        string account_id = account.AsMap().find("id")->second.AsString();
        string password = account.AsMap().find("password")->second.AsString();
        Player * player = (Player *)server.getObject(account_id);
        if (player) {
            Logout l = op;
            l.SetFrom(player->fullid);
            debug(cout << "Logout without from. Using " << player->fullid << " instead." << endl << flush;);
            operation(l);
        }
    }
    return oplist();
}

oplist Connection::Operation(const Get & op)
{
    cout << "Got get" << endl << flush;
    Info * info = new Info();
    *info = Info::Instantiate(); 
    Object::ListType args(1,server.asObject());
    info->SetArgs(args);
    info->SetRefno(op.GetSerialno());
    cout << "Replying to get" << endl << flush;
    
    return oplist(1,info);
}
