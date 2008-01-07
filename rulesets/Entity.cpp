// Cyphesis Online RPG Server and AI Engine
// Copyright (C) 2000-2005 Alistair Riddoch
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

// $Id: Entity.cpp,v 1.150 2008-01-07 00:06:18 alriddoch Exp $

#include "Entity.h"

#include "Script.h"

#include "common/log.h"
#include "common/debug.h"
#include "common/TypeNode.h"
#include "common/Property.h"
#include "common/PropertyManager.h"

#include <wfmath/atlasconv.h>

#include <Atlas/Objects/Operation.h>
#include <Atlas/Objects/Anonymous.h>

#include <sigc++/functors/mem_fun.h>

#include <cassert>

using Atlas::Message::Element;
using Atlas::Message::MapType;
using Atlas::Message::ListType;
using Atlas::Objects::Root;
using Atlas::Objects::Operation::Sight;
using Atlas::Objects::Entity::RootEntity;
using Atlas::Objects::Entity::Anonymous;

using Atlas::Objects::smart_dynamic_cast;

static const bool debug_flag = false;

/// \brief Entity constructor
Entity::Entity(const std::string & id, long intId) : LocatedEntity(id, intId),
                                         m_destroyed(false), m_motion(0),
                                         m_perceptive(false),
                                         m_update_flags(0)
{
    SignalProperty<BBox> * sp = new SignalProperty<BBox>(m_location.m_bBox, a_bbox);
    sp->modified.connect(sigc::mem_fun(&m_location, &Location::modifyBBox));
    m_properties["bbox"] = sp;
}

Entity::~Entity()
{
}

void Entity::setAttr(const std::string & name, const Element & attr)
{
    PropertyDict::const_iterator I = m_properties.find(name);
    if (I != m_properties.end()) {
        I->second->set(attr);
        m_update_flags |= I->second->flags();
        return;
    }
    PropertyBase * prop = PropertyManager::instance()->addProperty(this, name);
    if (prop == 0) {
        prop = new SoftProperty(attr);
    } else {
        prop->set(attr);
    }
    m_properties[name] = prop;
    m_update_flags |= prop->flags();
    return;
}

/// \brief Set the property object for a given attribute
///
/// @param name name of the attribute for which the property is given
/// @param prop the property object to be used
void Entity::setProperty(const std::string & name, PropertyBase * prop)
{
    m_properties[name] = prop;
}

/// \brief Copy attributes into an Atlas element
///
/// @param omap Atlas map element this entity should be copied into
void Entity::addToMessage(MapType & omap) const
{
    // We need to have a list of keys to pull from attributes.
    PropertyDict::const_iterator J = m_properties.begin();
    PropertyDict::const_iterator Jend = m_properties.end();
    for (; J != Jend; ++J) {
        J->second->add(J->first, omap);
    }
    omap["stamp"] = (double)m_seq;
    omap["parents"] = ListType(1, m_type);
    m_location.addToMessage(omap);
    omap["objtype"] = "obj";
}

/// \brief Copy attributes into an Atlas entity
///
/// @param ent Atlas entity this entity should be copied into
void Entity::addToEntity(const RootEntity & ent) const
{
    // We need to have a list of keys to pull from attributes.
    PropertyDict::const_iterator J = m_properties.begin();
    PropertyDict::const_iterator Jend = m_properties.end();
    for (; J != Jend; ++J) {
        J->second->add(J->first, ent);
    }
    ent->setStamp(m_seq);
    ent->setParents(std::list<std::string>(1, m_type->name()));
    m_location.addToEntity(ent);
    ent->setObjtype("obj");
}

/// \brief Install a handler function for an operation
///
/// @param class_no The class number of the operation to be handled
/// @param handler A pointer to the function to be wrapped
void Entity::installHandler(int class_no, Handler handler)
{
    m_operationHandlers.insert(std::make_pair(class_no, handler));
}

/// \brief Destroy this entity
///
/// Do the jobs required to remove this entity from the world. Handles
/// removing from the containership tree.
void Entity::destroy()
{
    assert(m_location.m_loc != 0);
    assert(m_location.m_loc->m_contains != 0);
    LocatedEntitySet & loc_contains = *m_location.m_loc->m_contains;
    if (m_contains != 0) {
        LocatedEntitySet::const_iterator Iend = m_contains->end();
        for (LocatedEntitySet::const_iterator I = m_contains->begin(); I != Iend; ++I) {
            Location & child = (*I)->m_location;
            // FIXME take account of orientation
            // FIXME velocity and orientation  need to be adjusted
            // Remove the reference to ourself.
            decRef();
            child.m_loc = m_location.m_loc;
            m_location.m_loc->incRef();
            if (m_location.orientation().isValid()) {
                child.m_pos = child.m_pos.toParentCoords(m_location.pos(),
                                                         m_location.orientation());
                child.m_velocity.rotate(m_location.orientation());
                child.m_orientation *= m_location.orientation();
            } else {
                static const Quaternion identity(1, 0, 0, 0);
                child.m_pos = child.m_pos.toParentCoords(m_location.pos(),
                                                         identity);
            }
            loc_contains.insert(*I);
        }
    }
    loc_contains.erase(this);

    // We don't call decRef() on our parent, because we may not get deleted
    // yet, and we need to keep a reference to our parent in case there
    // are broadcast ops left that we have not yet sent.

    if (loc_contains.empty()) {
        Entity * loc = dynamic_cast<Entity *>(m_location.m_loc);
        loc->m_update_flags |= a_cont;
        loc->updated.emit();
    }
    m_destroyed = true;
    destroyed.emit();
}

/// \brief Process an operation from an external source.
///
/// The ownership of the operation passed in at this point is handed
/// over to the entity. The calling code must not modify the operation
/// after passing it to externalOperation, or expect the attributes
/// of the operaration to remain the same.
/// @param op The operation to be processed.
void Entity::externalOperation(const Operation & op)
{
    OpVector res;
    operation(op, res);
    OpVector::const_iterator Iend = res.end();
    for (OpVector::const_iterator I = res.begin(); I != Iend; ++I) {
        if (!op->isDefaultSerialno()) {
            (*I)->setRefno(op->getSerialno());
        }
        sendWorld(*I);
    }
}

void Entity::operation(const Operation & op, OpVector & res)
{
    if (m_script->operation(op->getParents().front(), op, res) != 0) {
        return;
    }
    HandlerMap::const_iterator I = m_operationHandlers.find(op->getClassNo());
    if (I != m_operationHandlers.end()) {
        debug(std::cout << "Found handler for " << op->getParents().front()
                        << " operations" << std::endl << std::flush;);
        I->second(this, op, res);
    }
    return callOperation(op, res);
}

void Entity::onContainered()
{
    containered.emit();

    containered.clear();
}
