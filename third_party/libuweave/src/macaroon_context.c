// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/macaroon_context.h"

bool uw_macaroon_context_create_(uint32_t current_time,
                                 const uint8_t* ble_session_id,
                                 size_t ble_session_id_len,
                                 const uint8_t* auth_challenge_str,
                                 size_t auth_challenge_str_len,
                                 UwMacaroonContext* new_context) {
  if ((ble_session_id == NULL && ble_session_id_len != 0) ||
      (auth_challenge_str == NULL && auth_challenge_str_len != 0)) {
    return false;
  }
  if (new_context == NULL) {
    return false;
  }

  *new_context = (UwMacaroonContext){};
  new_context->current_time = current_time;
  new_context->ble_session_id = ble_session_id;
  new_context->ble_session_id_len = ble_session_id_len;
  new_context->auth_challenge_str = auth_challenge_str;
  new_context->auth_challenge_str_len = auth_challenge_str_len;
  return true;
}
