// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2000 Alistair Riddoch

#include <Atlas/Message/Object.h>
#include <Atlas/Objects/Root.h>
#include <Atlas/Objects/Operation/Login.h>
#include <Atlas/Objects/Operation/Combine.h>
#include <Atlas/Objects/Operation/Create.h>
#include <Atlas/Objects/Operation/Sound.h>
#include <Atlas/Objects/Operation/Create.h>
#include <Atlas/Objects/Operation/Set.h>
#include <Atlas/Objects/Operation/Delete.h>
#include <Atlas/Objects/Operation/Divide.h>
#include <Atlas/Objects/Operation/Look.h>
#include <Atlas/Objects/Operation/Create.h>
#include <Atlas/Objects/Operation/Talk.h>
#include <Atlas/Objects/Operation/Touch.h>
#include <Atlas/Objects/Operation/Sight.h>
#include <Atlas/Objects/Operation/Move.h>
#include <Atlas/Objects/Operation/Error.h>

#include <common/Setup.h>
#include <common/Tick.h>
#include <common/Cut.h>
#include <common/Chop.h>
#include <common/Eat.h>
#include <common/Nourish.h>
#include <common/Fire.h>


#include <varconf/Config.h>

extern "C" {
    #include <stdlib.h>
}

// #define DEBUG_MOVEMENT


#include "Character.h"
#include "Pedestrian.h"

#include "BaseMind.h"
#include "ExternalMind.h"
#include "Script.h"
#include "Python_API.h" // FIXME This must go

#include <server/WorldRouter.h>

#include <modules/Location.h>

#include <common/op_switch.h>
#include <common/const.h>
#include <common/debug.h>
#include <common/globals.h>

using Atlas::Message::Object;

static const bool debug_flag = false;

oplist Character::metabolise(double ammount = 1)
{
    // Currently handles energy
    // We should probably call this whenever the entity performs a movement.
    Object::MapType ent;
    ent["id"] = fullid;
    if ((status > (1.5 + energyLoss)) && (weight < maxWeight)) {
        status = status - energyLoss;
        ent["weight"] = weight + weightGain;
    }
    double energyUsed = energyConsumption * ammount;
    if ((status <= energyUsed) && (weight > weightConsumption)) {
        ent["status"] = status - energyUsed + energyGain;
        ent["weight"] = weight - weightConsumption;
    } else {
        ent["status"] = status - energyUsed;
    }

    Set * s = new Set(Set::Instantiate());
    s->SetTo(fullid);
    s->SetArgs(Object::ListType(1,ent));

    return oplist(1,s);
}

Character::Character() : movement(*new Pedestrian(*this)), autom(0), mind(NULL),
                         externalMind(NULL), drunkness(0.0),
                         sex("female"), food(0), maxWeight(100)
{
    isCharacter = true;
    weight = 60;
    location.bbox = Vector3D(0.25, 0.25, 1);
    location.bmedian = Vector3D(0, 0, 1);
}

Character::~Character()
{
    if (mind != NULL) {
        delete mind;
    }
    if (externalMind != NULL) {
        delete externalMind;
    }
}

const Object & Character::operator[](const string & aname)
{
    if (aname == "drunkness") {
        attributes[aname] = Object(drunkness);
    } else if (aname == "sex") {
        attributes[aname] = Object(sex);
    }
    return Thing::operator[](aname);
}

void Character::set(const string & aname, const Object & attr)
{
    if ((aname == "drunkness") && attr.IsFloat()) {
        drunkness = attr.AsFloat();
    } else if ((aname == "sex") && attr.IsString()) {
        sex = attr.AsString();
    } else {
        Thing::set(aname, attr);
    }
}

void Character::addToObject(Object & obj) const
{
    Object::MapType & omap = obj.AsMap();
    omap["weight"] = Object(weight);
    omap["sex"] = Object(sex);
    Thing::addToObject(obj);
}

oplist Character::Operation(const Setup & op)
{
    debug( cout << "Character::tick" << endl << flush;);
    oplist res;
    debug( cout << "CHaracter::Operation(setup)" << endl << flush;);
    if (script->Operation("setup", op, res) != 0) {
        return res;
    }
    if (op.HasAttr("sub_to")) {
        debug( cout << "Has sub_to" << endl << flush;);
        return res;
    }

    mind = new BaseMind(fullid, name);
    string mind_class("NPCMind"), mind_package("mind.NPCMind");
    if (global_conf->findItem("mind", type)) {
        mind_package = global_conf->getItem("mind", type);
        mind_class = type + "Mind";
    }
    Create_PyMind(mind, mind_package, mind_class);

    oplist res2(2);
    Setup * s = new Setup(op);
    // THis is so not the right thing to do
    s->SetAttr("sub_to", Object("mind"));
    res2[0] = s;
    Look * l = new Look(Look::Instantiate());
    l->SetTo(world->fullid);
    res2[1] = l;
    if (location.ref != world) {
        l = new Look(Look::Instantiate());
        l->SetTo(location.ref->fullid);
        res2.push_back(l);
    }
    l = new Look(Look::Instantiate());
    l->SetTo(fullid);
    res2.push_back(l);
    RootOperation * tick = new Tick(Tick::Instantiate());
    tick->SetTo(fullid);
    res2.push_back(tick);
    return(res2);
}

oplist Character::Operation(const Tick & op)
{
    if (op.HasAttr("sub_to")) {
        debug( cout << "Has sub_to" << endl << flush;);
        return oplist();
    }
    debug(cout << "================================" << endl << flush;);
    const Object::ListType & args = op.GetArgs();
    if ((0 != args.size()) && (args.front().IsMap())) {
        // Deal with movement.
        Object::MapType arg1 = args.front().AsMap();
        if ((arg1.find("serialno") != arg1.end()) &&
           (arg1["serialno"].IsInt())) {
            if (arg1["serialno"].AsInt() < movement.serialno) {
                debug(cout << "Old tick" << endl << flush;);
                return oplist();
            }
        }
        Location ret_loc;
        RootOperation * moveOp = movement.genMoveOperation(&ret_loc);
        if (moveOp) {
            oplist res(2);
            Object::MapType entmap;
            entmap["name"]=Object("move");
            entmap["serialno"]=Object(movement.serialno);
            Object ent(entmap);
            RootOperation * tickOp = new Tick(Tick::Instantiate());
            tickOp->SetTo(fullid);
            tickOp->SetFutureSeconds(movement.getTickAddition(ret_loc.coords));
            tickOp->SetArgs(Object::ListType(1,ent));
            res[0] = tickOp;
            res[1] = moveOp;
            return res;
        }
    } else {
        oplist res;
        script->Operation("tick", op, res);

        // DIGEST
        if ((food >= foodConsumption) && (status < 2)) {
            // It is important that the metabolise bit is done next, as this
            // handles the status change
            status = status + foodConsumption;
            food = food - foodConsumption;

            Object::MapType food_ent;
            food_ent["id"] = fullid;
            food_ent["food"] = food;
            Set s = Set::Instantiate();
            s.SetTo(fullid);
            s.SetArgs(Object::ListType(1,food_ent));

            Sight * si = new Sight(Sight::Instantiate());
            si->SetTo(fullid);
            si->SetArgs(Object::ListType(1,s.AsObject()));
            res.push_back(si);
        }

        // METABOLISE
        oplist mres = metabolise();
        for(oplist::const_iterator I = mres.begin(); I != mres.end(); I++) {
            res.push_back(*I);
        }
        
        // TICK
        RootOperation * tickOp = new Tick(Tick::Instantiate());
        tickOp->SetTo(fullid);
        tickOp->SetFutureSeconds(consts::basic_tick * 30);
        res.push_back(tickOp);
        return res;
    }
    return oplist();
}

oplist Character::Operation(const Talk & op)
{
    debug( cout << "Character::OPeration(Talk)" << endl << flush;);
    Sound * s = new Sound(Sound::Instantiate());
    Object::ListType args(1,op.AsObject());
    s->SetArgs(args);
    return oplist(1,s);
}

oplist Character::Operation(const Eat & op)
{
    // This is identical to Foof::Operation(Eat &)
    // Perhaps animal should inherit from Food?
    oplist res;
    if (script->Operation("eat", op, res) != 0) {
        return(res);
    }
    Object::MapType self_ent;
    self_ent["id"] = fullid;
    self_ent["status"] = -1;

    Set * s = new Set(Set::Instantiate());
    s->SetTo(fullid);
    s->SetArgs(Object::ListType(1,self_ent));

    const string & to = op.GetFrom();
    Object::MapType nour_ent;
    nour_ent["id"] = to;
    nour_ent["weight"] = weight;
    Nourish * n = new Nourish(Nourish::Instantiate());
    n->SetTo(to);
    n->SetArgs(Object::ListType(1,nour_ent));

    oplist res2;
    res2[0] = s;
    res2[1] = n;
    return res2;
}

oplist Character::Operation(const Nourish & op)
{
    Object::MapType nent = op.GetArgs().front().AsMap();
    food = food + nent["weight"].AsNum();

    Object::MapType food_ent;
    food_ent["id"] = fullid;
    food_ent["food"] = food;
    Set s = Set::Instantiate();
    s.SetArgs(Object::ListType(1,food_ent));

    Sight * si = new Sight(Sight::Instantiate());
    si->SetTo(fullid);
    si->SetArgs(Object::ListType(1,s.AsObject()));
    return oplist(1,si);
}

oplist Character::mindOperation(const Login & op)
{
    return oplist();
}

oplist Character::mindOperation(const Setup & op)
{
    Setup *s = new Setup(op);
    s->SetTo(fullid);
    s->SetAttr("sub_to", Object("mind"));
    return oplist(1,s);
}

oplist Character::mindOperation(const Tick & op)
{
    Tick *t = new Tick(op);
    t->SetTo(fullid);
    t->SetAttr("sub_to", Object("mind"));
    return oplist(1,t);
}

oplist Character::mindOperation(const Move & op)
{
    debug( cout << "Character::mind_move_op" << endl << flush;);
    Move * newop = new Move(op);
    const Object::ListType & args = op.GetArgs();
    if ((0 == args.size()) || (!args.front().IsMap())) {
        cerr << "move op has no argument" << endl << flush;
        return oplist();
    }
    Object::MapType arg1 = args.front().AsMap();
    if ((arg1.find("id") == arg1.end()) || !arg1["id"].IsString()) {
        cerr << "Its got no id" << endl << flush;
    }
    string & oname = arg1["id"].AsString();
    if (world->objects.find(oname) == world->objects.end()) {
        debug( cout << "This move op is for a phoney object" << endl << flush;);
        delete newop;
        return oplist();
    }
    Thing * obj = (Thing *)world->objects[oname];
    if (obj != this) {
        debug( cout << "Moving something else. " << oname << endl << flush;);
        if ((obj->weight < 0) || (obj->weight > weight)) {
            debug( cout << "We can't move this. Just too heavy" << endl << flush;);
            delete newop;
            return oplist();
        }
        newop->SetTo(oname);
        return oplist(1,newop);
    }
    string location_ref;
    if ((arg1.find("loc") != arg1.end()) && (arg1["loc"].IsString())) {
        location_ref = arg1["loc"].AsString();
    } else {
        debug( cout << "Parent not set" << endl << flush;);
    }
    Vector3D location_coords;
    if ((arg1.find("pos") != arg1.end()) && (arg1["pos"].IsList())) {
        Object::ListType vector = arg1["pos"].AsList();
        if (vector.size()==3) {
            try {
                // FIXME
                //double x = vector.front().AsFloat();
                //vector.pop_front();
                //double y = vector.front().AsFloat();
                //vector.pop_front();
                //double z = vector.front().AsFloat();
                double x = vector[0].AsFloat();
                double y = vector[1].AsFloat();
                double z = vector[2].AsFloat();
                location_coords = Vector3D(x, y, z);
                debug( cout << "Got new format coords: " << location_coords << endl << flush;);
            }
            catch (Atlas::Message::WrongTypeException) {
                cerr << "EXCEPTION: Malformed pos move operation" << endl << flush;
            }
        }
    }

    Vector3D location_vel;
    if ((arg1.find("velocity") != arg1.end()) && (arg1["velocity"].IsList())) {
        Object::ListType vector = arg1["velocity"].AsList();
        if (vector.size()==3) {
            try {
                // FIXME
                //double x = vector.front().AsFloat();
                //vector.pop_front();
                //double y = vector.front().AsFloat();
                //vector.pop_front();
                //double z = vector.front().AsFloat();
                double x = vector[0].AsFloat();
                double y = vector[1].AsFloat();
                double z = vector[2].AsFloat();
                location_vel = Vector3D(x, y, z);
                debug( cout << "Got new format velocity: " << location_vel << endl << flush;);
            }
            catch (Atlas::Message::WrongTypeException) {
                cerr << "EXCEPTION: Malformed vel move operation" << endl << flush;
            }
        }
    }
    Vector3D location_face;
    if ((arg1.find("face") != arg1.end()) && (arg1["face"].IsList())) {
        Object::ListType vector = arg1["face"].AsList();
        if (vector.size()==3) {
            try {
                // FIXME
                //double x = vector.front().AsFloat();
                //vector.pop_front();
                //double y = vector.front().AsFloat();
                //vector.pop_front();
                //double z = vector.front().AsFloat();
                double x = vector[0].AsFloat();
                double y = vector[1].AsFloat();
                double z = vector[2].AsFloat();
                location_face = Vector3D(x, y, z);
                debug( cout << "Got new format face: " << location_face << endl << flush;);
            }
            catch (Atlas::Message::WrongTypeException) {
                cerr << "EXCEPTION: Malformed face move operation" << endl << flush;
            }
        }
    }
    if (!location_coords) {
        if (op.GetFutureSeconds() < 0) {
            newop->SetFutureSeconds(0);
        }
    } else {
        location_coords = location_coords +
            (Vector3D(((double)rand())/RAND_MAX, ((double)rand())/RAND_MAX, 0)
				* drunkness * 10);
    }
    double vel_mag;
    // Print out a bunch of debug info
    debug( cout << ":" << location_ref << ":" << world->fullid << ":" << location.ref->fullid << ":" << endl << flush;);
    if ( //(location_ref==world->fullid) &&
         (location_ref==location.ref->fullid) &&
         (newop->GetFutureSeconds() >= 0) ) {
        // Movement within current ref. Work out the speed and stuff and
        // use movementinfo to track movement.
        if (!location_vel) {
            debug( cout << "\tVelocity default" << endl << flush;);
            vel_mag=consts::base_velocity;
        } else {
            vel_mag=location_vel.mag();
            if (vel_mag > consts::base_velocity) {
                vel_mag = consts::base_velocity;
            }
            debug( cout << "\tVelocity given: " << location_vel
                                   << "," << vel_mag << endl << flush;);
        }
        if (location_face) {
            location.face = location_face;
        }

        Vector3D direction;
        if (location_coords == location.coords) {
            location_coords = Vector3D();
        }
        if (!location_coords) {
            if (!location_vel || (location_vel==Vector3D(0,0,0))) {
                debug( cout << "\tUsing face for direction" << endl << flush;);
                direction=location.face;
            } else {
                debug( cout << "\tUsing velocity for direction" << endl << flush;);
                direction=location_vel;
            }
        } else {
            debug( cout << "\tUsing destination coords for direction" << endl << flush;);
            direction=location_coords-location.coords;
        }
        if (!direction) {
            debug( cout << "No direction" << endl << flush;);
        } else {
            direction=direction.unitVector();
            debug( cout << "Direction: " << direction << endl << flush;);
        }
        if (!location_face) {
            location.face = direction;
        }
        Location ret_location;
        RootOperation * moveOp = movement.genMoveOperation(&ret_location);
        const Location & current_location = (NULL!=moveOp) ? ret_location : location;
        //if (NULL!=moveOp) {
            //current_location = ret_location;
        //} else {
            //current_location = location;
        //}
        movement.reset();
        if ((vel_mag==0) || !direction) {
            debug( cout << "\tMovement stopped" << endl << flush;);
            if (NULL != moveOp) {
                Object::ListType & args = moveOp->GetArgs();
                Object::MapType & ent = args.front().AsMap();
                Object::ListType velocity;
                velocity.push_back(Object(0.0));
                velocity.push_back(Object(0.0));
                velocity.push_back(Object(0.0));
                ent["velocity"]=Object(velocity);
                ent["mode"]=Object("standing");
                moveOp->SetArgs(args);
            } else {
                moveOp = movement.genFaceOperation(location);
            }
            delete newop;
            if (NULL != moveOp) {
                return oplist(1,moveOp);
            }
            return oplist();
        }
        RootOperation * tickOp = new Tick(Tick::Instantiate());
        Object::MapType ent;
        ent["serialno"] = Object(movement.serialno);
        ent["name"] = Object("move");
        Object::ListType args(1,ent);
        tickOp->SetArgs(args);
        tickOp->SetTo(fullid);
        // Need to add the arguments to this op before we return it
        // direction is already a unit vector
        if (!location_coords) {
            debug( cout << "\tNo target location" << endl << flush;);
            movement.targetLocation = Vector3D();
        } else {
            debug( cout << "\tUsing target location" << endl << flush;);
            movement.targetLocation = location_coords;
        }
        movement.velocity=direction*vel_mag;
        debug( cout << "Velocity " << vel_mag << endl << flush;);
        RootOperation * moveOp2 = movement.genMoveOperation(NULL,current_location);
        tickOp->SetFutureSeconds(movement.getTickAddition(location.coords));
        debug( cout << "Next tick " << tickOp->GetFutureSeconds() << endl << flush;);
        if (NULL!=moveOp2) {
            if (NULL!=moveOp) {
                delete moveOp;
            }
            moveOp=moveOp2;
        }
        // return moveOp and tickOp;
        oplist res(2);
        res[0] = moveOp;
        res[1] = tickOp;
        delete newop;
        return(res);
    }
    return oplist(1,newop);
}

oplist Character::mindOperation(const Set & op)
{
    Set * s = new Set(op);
    const Object::ListType & args = op.GetArgs();
    if (args.front().IsMap()) {
        Object::MapType amap = args.front().AsMap();
        if (amap.find("id") != amap.end() && amap["id"].IsString()) {
            string opid = amap["id"].AsString();
            if (opid != fullid) {
                s->SetTo(opid);
            }
        }
    }
    return oplist(1,s);
}

oplist Character::mindOperation(const Sight & op)
{
    return oplist();
}

oplist Character::mindOperation(const Sound & op)
{
    return oplist();
}

oplist Character::mindOperation(const Create & op)
{
    // We need to call, THE THING FACTORY! Or maybe not
    // This currently does nothing, so characters are not able to directly
    // create objects. By and large a tool must be used. This should at some
    // point be able to send create operations on to other entities
    debug( cout << "Character::mindOperation(Create)" << endl << flush;);
    return oplist();
}

oplist Character::mindOperation(const Delete & op)
{
    Delete * d = new Delete(op);
    return oplist(1,d);
}

oplist Character::mindOperation(const Talk & op)
{
    debug( cout << "Character::mindOPeration(Talk)" << endl << flush;);
    Talk * t = new Talk(op);
    return oplist(1,t);
}

oplist Character::mindOperation(const Look & op)
{
    debug( cout << "Got look up from mind from [" << op.GetFrom()
                            << "] to [" << op.GetTo() << "]" << endl << flush;);
    perceptive = true;
    Look * l = new Look(op);
    if (op.GetTo().size() == 0) {
        const Object::ListType & args = op.GetArgs();
        if (args.size() == 0) {
            l->SetTo(world->fullid);
        } else {
            if (args.front().IsMap()) {
                Object::MapType amap = args.front().AsMap();
                if (amap.find("id") != amap.end() && amap["id"].IsString()) {
                    l->SetTo(amap["id"].AsString());
                }
            }
        }
    }
    debug( cout <<"    now to ["<<l->GetTo()<<"]"<<endl<<flush;);
    return oplist(1,l);
}

oplist Character::mindOperation(const Load & op)
{
    return oplist();
}

oplist Character::mindOperation(const Save & op)
{
    return oplist();
}

oplist Character::mindOperation(const Cut & op)
{
    Cut * c = new Cut(op);
    return oplist(1,c);
}

oplist Character::mindOperation(const Eat & op)
{
    Eat * e = new Eat(op);
    return oplist(1,e);
}

oplist Character::mindOperation(const Touch & op)
{
    Touch * t = new Touch(op);
    // Work out what is being touched.
    const Object::ListType & args = op.GetArgs();
    if ((op.GetTo().size() == 0) || (args.size() != 0)) {
        if (args.size() == 0) {
            t->SetTo(world->fullid);
        } else {
            if (args.front().IsMap()) {
                Object::MapType amap = args.front().AsMap();
                if (amap.find("id") != amap.end() && amap["id"].IsString()) {
                    t->SetTo(amap["id"].AsString());
                }
            } else if (args.front().IsString()) {
                t->SetTo(args.front().AsString());
            }
        }
    }
    // Pass the modified touch operation on to target.
    oplist res(2);
    res[0] = t;
    // Set our mode to "touching"
    Set * s = new Set(Set::Instantiate());
    s->SetTo(fullid);
    Object::MapType amap;
    amap["id"] = fullid;
    amap["mode"] = "touching";
    Object::ListType setArgs(1,Object(amap));
    s->SetArgs(setArgs);
    res[1] = s;
    return res;
}

oplist Character::mindOperation(const Appearance & op)
{
    return oplist();
}

oplist Character::mindOperation(const Disappearance & op)
{
    return oplist();
}


oplist Character::mindOperation(const Error & op)
{
    return oplist();
}

oplist Character::mindOperation(const RootOperation & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Login & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Chop & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Create & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Cut & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Delete & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Eat & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Fire & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Move & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Set & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Look & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Load & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Save & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Appearance & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Disappearance & op)
{
    return oplist();
}

oplist Character::w2mOperation(const RootOperation & op)
{
    return oplist();
}

oplist Character::w2mOperation(const Error & op)
{
    Error * e = new Error(op);
    return oplist(1,e);
}

oplist Character::w2mOperation(const Setup & op)
{
    if (op.HasAttr("sub_to")) {
        Setup * s = new Setup(op);
        return oplist(1,s);
    }
    return oplist();
}

oplist Character::w2mOperation(const Tick & op)
{
    if (op.HasAttr("sub_to")) {
        Tick * t = new Tick(op);
        return oplist(1,t);
    }
    return oplist();
}

oplist Character::w2mOperation(const Sight & op)
{
    if (drunkness > 1.0) {
        return oplist();
    }
    Sight * s = new Sight(op);
    return oplist(1,s);
}

oplist Character::w2mOperation(const Sound & op)
{
    if (drunkness > 1.0) {
        return oplist();
    }
    Sound * s = new Sound(op);
    return oplist(1,s);
}

oplist Character::w2mOperation(const Touch & op)
{
    if (drunkness > 1.0) {
        return oplist();
    }
    Touch * t = new Touch(op);
    return oplist(1,t);
}

oplist Character::sendMind(const RootOperation & op)
{
    debug( cout << "Character::sendMind" << endl << flush;);
    if (mind == NULL) {
        return oplist();
    }
    oplist local_res = mind->message(op);
    oplist external_res;

    if (NULL != externalMind) {
        debug( cout << "Sending to external mind" << endl << flush;);
        external_res = externalMind->message(op);
        // If there is some kinf of error in the connection, we turn autom on
    } else {
        //return(*(RootOperation **)NULL);
        if (autom == 0) {
            debug( cout << "Turning automatic on for " << fullid << endl << flush;);
            autom = 1;
            if (externalMind != NULL) {
                delete externalMind;
                externalMind = NULL;
            }
        }
    }
    if (autom) {
        debug( cout << "Using " << local_res.size() << " ops from local mind"
             << endl << flush;);
        //res = local_res;
    } else {
        debug( cout << "Using " << local_res.size() << " ops from external mind"
             << endl << flush;);
        //res = external_res;
    }
    const oplist & res = autom ? local_res : external_res;
    // At this point there is a bunch of conversion stuff that I don't
    // understand
    
    return(res);
}

oplist Character::mind2body(const RootOperation & op)
{
    debug( cout << "Character::mind2body" << endl << flush;);
    RootOperation newop(op);

    if ((newop.GetTo().size() == 0) &&
        (op.GetParents().front().AsString() != "look")) {
       newop.SetTo(fullid);
    }
    if (drunkness > 1.0) {
        return oplist();
    }
    op_no_t otype = opEnumerate(newop);
    OP_SWITCH(newop, otype, mind)
}

oplist Character::world2body(const RootOperation & op)
{
    debug( cout << "Character::world2body" << endl << flush;);
    return callOperation(op);
}

oplist Character::world2mind(const RootOperation & op)
{
    debug( cout << "Character::world2mind" << endl << flush;);
    op_no_t otype = opEnumerate(op);
    OP_SWITCH(op, otype, w2m)
}

oplist Character::externalMessage(const RootOperation & op)
{
    debug( cout << "Character::externalMessage" << endl << flush;);
    return externalOperation(op);
}

oplist Character::operation(const RootOperation & op)
{
    debug( cout << "Character::operation" << endl << flush;);
    oplist result = world2body(op);
    // set refno on result?
    oplist mres = world2mind(op);
    // set refno on mres?
    for(oplist::const_iterator I = mres.begin(); I != mres.end(); I++) {
        //RootOperation * mr = mind_res.front();
        oplist mres2 = sendMind(**I);
        for(oplist::const_iterator J = mres2.begin(); J != mres2.end(); J++) {
            //RootOperation * mr2 = mind2_res.front();
            // Need to be very careful about what this actually does
            externalMessage(**J);
            delete *J;
        }
        delete *I;
    }
    return result;
}

oplist Character::externalOperation(const RootOperation & op)
{
    debug( cout << "Character::externalOperation" << endl << flush;);
    oplist body_res = mind2body(op);
    // set refno on body_res?
    
    for(oplist::const_iterator I = body_res.begin(); I != body_res.end(); I++) {
        sendWorld(*I);
        // Don't delete br as it has gone into worlds queue
        // World will deal with it.
    }
    return oplist();
}
