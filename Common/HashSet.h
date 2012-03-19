#ifndef HASHSET_H
#define HASHSET_H 1

#include "config.h"

#if HAVE_UNORDERED_SET
# include <unordered_set>
# define hash_set std::unordered_set
#elif HAVE_TR1_UNORDERED_SET
# include <tr1/unordered_set>
# define hash_set std::tr1::unordered_set
#elif HAVE_BACKWARD_HASH_SET || HAVE_EXT_HASH_SET || HAVE_HASH_SET
# undef __DEPRECATED
# if HAVE_BACKWARD_HASH_SET
#  include <backward/hash_set>
# elif HAVE_EXT_HASH_SET
#  include <ext/hash_set>
# elif HAVE_HASH_SET
#  include <hash_set>
# endif
using __gnu_cxx::hash_set;
#else
#  error A hash set implementation is required.
#endif

#endif
