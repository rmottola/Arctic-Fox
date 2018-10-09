/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIAccessibleRelation.h"
#include "nsIAccessibleRole.h"
#include "RelationType.h"
#include "Role.h"

using namespace mozilla::a11y;

#define ROLE(goannaRole, stringRole, atkRole, macRole, msaaRole, ia2Role, nameRule) \
  static_assert(static_cast<uint32_t>(roles::goannaRole) \
                == static_cast<uint32_t>(nsIAccessibleRole::ROLE_ ## goannaRole), \
                "internal and xpcom roles differ!");
#include "RoleMap.h"
#undef ROLE

#define RELATIONTYPE(goannaType, stringType, atkType, msaaType, ia2Type) \
  static_assert(static_cast<uint32_t>(RelationType::goannaType) \
                == static_cast<uint32_t>(nsIAccessibleRelation::RELATION_ ## goannaType), \
                "internal and xpcom relations differ!");
#include "RelationTypeMap.h"
#undef RELATIONTYPE
