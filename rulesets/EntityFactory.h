// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2000 Alistair Riddoch

#ifndef ENTITY_FACTORY_H
#define ENTITY_FACTORY_H

#include <string>

#include <common/BaseEntity.h>

class Thing;
class ThingFactory;

class EntityFactory {
    EntityFactory();
    static EntityFactory * m_instance;

    std::map<std::string, ThingFactory *> factories;
  public:
    static EntityFactory * instance() {
        if (m_instance == NULL) {
            m_instance = new EntityFactory();
        }
        return m_instance;
    }
    void readRuleset(const string & file);
    Thing * newThing(const string &, const Atlas::Message::Object &, Routing *);
};

#endif /* ENTITY_FACTORY_H */
