#ifndef __WEBHOOKS_OAUTH_H
#define __WEBHOOKS_OAUTH_H

RETRO_BEGIN_DECLS

typedef void (*access_token_callback_t)(const char*);

void woauth_initiate_pairing
(
  void
);

bool woauth_is_pairing
(
  void
);

void woauth_abort_pairing
(
  void
);

void woauth_load_accesstoken
(
  access_token_callback_t on_access_token_retrieved_callback
);

RETRO_END_DECLS

#endif //__WEBHOOKS_OAUTH_H