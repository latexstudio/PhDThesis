#pragma once

#include <ostream>
#include "datastore/datastore.h"

using namespace std;

ostream & Graphviz(ostream & out, const t_datastore & ds);
