// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2000 Alistair Riddoch

#ifndef PLANT_H
#define PLANT_H

#include "Thing.h"

// This is the base class for flowering plants. Most of the functionality
// will be common to all plants, and most derived classes will probably
// be in python.


class Plant : public Thing {
  protected:
    int fruits; // Number of fruits on the plant
    int radius; // Proportion of height as radius
    int fruitChance; // chance of growing fruit
    double sizeAdult; // chance of growing fruit
    std::string fruitName;

    static const int speed = 20; // Number of basic_ticks per tick
    static const int minuDrop = 0; // min fruit dropped
    static const int maxuDrop = 2; // max fruit dropped

    int dropFruit(oplist & res);
  public:

    Plant();
    virtual ~Plant() { }

    virtual const Object & operator[](const string & aname);
    virtual void set(const string & aname, const Object & attr);

    virtual oplist Operation(const Tick & op);
};

#endif // PLANT_H
