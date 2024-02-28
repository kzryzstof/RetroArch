#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <compat/strl.h>

#include "include/webhooks.h"
#include "include/webhooks_client.h"
#include "include/webhooks_oauth.h"

#include "../deps/rcheevos/include/rc_api_runtime.h"
#include "../deps/rcheevos/src/rapi/rc_api_common.h"

//  ---------------------------------------------------------------------------
//  Frees up the allocated memory related to the request.
//  ---------------------------------------------------------------------------
static void wc_end_http_request
(
  async_http_request_t* request
)
{
  rc_api_destroy_request(&request->request);

  free(request->headers);
  request->headers = NULL;
  
  free(request);
}

//  ---------------------------------------------------------------------------
//  Called back when the game event has been successfully sent.
//  ---------------------------------------------------------------------------
static void wc_on_game_event_sent_completed
(
  async_http_request_t *request,
  http_transfer_data_t *data,
  char buffer[],
  size_t buffer_size
)
{
  void (*on_game_event_sent_callback)() = request->callback;
  unsigned short game_event = (unsigned short)(request->callback_data);

  WEBHOOKS_LOG(WEBHOOKS_TAG "Game event '%d' has been sent\n", game_event);

  if (on_game_event_sent_callback != NULL)
    on_game_event_sent_callback();
}

//  ---------------------------------------------------------------------------
//  Called back when the response has been received (whether it is successful or not.
//  ---------------------------------------------------------------------------
static void wc_handle_http_callback
(
  retro_task_t* task,
  void* task_data,
  void* user_data,
  const char* error
)
{
  struct async_http_request_t *request = (struct async_http_request_t*)user_data;
  http_transfer_data_t *data = (http_transfer_data_t*)task_data;
  char buffer[224];

  if (error)
  {
    strlcpy(buffer, error, sizeof(buffer));
  }
  else if (!data)
  {
    //  Server did not return HTTP headers
    strlcpy(buffer, "Server communication error", sizeof(buffer));
  }
  else
  {
    if (data->status <= 0)
    {
      //  Something occurred which prevented the response from being processed.
      //  assume the server request hasn't happened and try again.
      snprintf(buffer, sizeof(buffer), "task status code %d", data->status);
      return;
    }

    if (data->status <= 299)
    {
      snprintf(buffer, sizeof(buffer), "HTTP status code %d", data->status);
      
      //  Indicate success unless handler provides error
      buffer[0] = '\0';

      //  Call appropriate handler to process the response
      if (request->handler)
        request->handler(request, data, buffer, sizeof(buffer));
      
    }
    else if (data->status > 299)
    {
      snprintf(buffer, sizeof(buffer), "HTTP error code %d", data->status);
    }
    else {
      //  Server sent empty response without error status code
      strlcpy(buffer, "No response from server", sizeof(buffer));
    }
  }

  if (!buffer[0])
  {
    if (request->success_message)
    {
      if (request->id)
        WEBHOOKS_LOG(WEBHOOKS_TAG "%s %u\n", request->success_message, request->id);
      else
        WEBHOOKS_LOG(WEBHOOKS_TAG "%s\n", request->success_message);
    }
  }
  else
  {
    char errbuf[256];
    
    if (request->id)
      snprintf(errbuf, sizeof(errbuf), "%s %u: %s", request->failure_message, request->id, buffer);
    else
      snprintf(errbuf, sizeof(errbuf), "%s: %s", request->failure_message, buffer);

    WEBHOOKS_LOG(WEBHOOKS_TAG "%s\n", errbuf);
  }

  wc_end_http_request(request);
}

//  ---------------------------------------------------------------------------
//  Sends the HTTP request to the server.
//  ---------------------------------------------------------------------------
static void wc_begin_http_request
(
  async_http_request_t* request
)
{
  task_push_http_post_transfer_with_headers
  (
    request->request.url,
    request->request.post_data,
    true,
    "POST",
    request->headers,
    wc_handle_http_callback,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Configures the body of the HTTP request when sending progress.
//  ---------------------------------------------------------------------------
static void wc_set_progress_request_url
(
  wc_game_event_t game_event,
  const char* progress,
  async_http_request_t* request
)
{
  const settings_t *settings = config_get_ptr();
  request->request.url = settings->arrays.webhook_url;
  
  rc_api_url_builder_t builder;
  rc_url_builder_init(&builder, &request->request.buffer, 48);

  char time_str[64];
  sprintf(time_str, "%lld", game_event.time);
  
  char frame_number_str[64];
  sprintf(frame_number_str, "%ld", game_event.frame_number);

  rc_url_builder_append_str_param(&builder, "h", game_event.rom_hash);
  rc_url_builder_append_num_param(&builder, "c", game_event.console_id);
  rc_url_builder_append_str_param(&builder, "p", progress);
  rc_url_builder_append_str_param(&builder, "f", frame_number_str);
  rc_url_builder_append_str_param(&builder, "t", time_str);
  request->request.post_data = rc_url_builder_finalize(&builder);
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wc_set_event_request_url
(
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  const settings_t *settings = config_get_ptr();
  request->request.url = settings->arrays.webhook_url;

  rc_api_url_builder_t builder;
  rc_url_builder_init(&builder, &request->request.buffer, 48);

  char time_str[64];
  sprintf(time_str, "%lld", game_event.time);
  
  char frame_number_str[64];
  sprintf(frame_number_str, "%ld", game_event.frame_number);

  rc_url_builder_append_str_param(&builder, "h", game_event.rom_hash);
  rc_url_builder_append_num_param(&builder, "c", game_event.console_id);
  rc_url_builder_append_num_param(&builder, "e", game_event.game_event_id);
  rc_url_builder_append_str_param(&builder, "f", frame_number_str);
  rc_url_builder_append_str_param(&builder, "t", time_str);
  request->request.post_data = rc_url_builder_finalize(&builder);
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wc_set_achievement_request_url
(
  wc_game_event_t game_event,
  wc_achievement_event_t achievement_event,
  async_http_request_t* request
)
{
  const settings_t *settings = config_get_ptr();
  request->request.url = settings->arrays.webhook_url;

  rc_api_url_builder_t builder;
  rc_url_builder_init(&builder, &request->request.buffer, 48);

  char time_str[64];
  sprintf(time_str, "%lld", game_event.time);

  char frame_number_str[64];
  sprintf(frame_number_str, "%ld", game_event.frame_number);

  rc_url_builder_append_str_param(&builder, "h", game_event.rom_hash);
  rc_url_builder_append_num_param(&builder, "c", game_event.console_id);
  rc_url_builder_append_num_param(&builder, "e", game_event.game_event_id);
  rc_url_builder_append_num_param(&builder, "a", achievement_event.active);
  rc_url_builder_append_num_param(&builder, "b", achievement_event.total);
  rc_url_builder_append_str_param(&builder, "d", achievement_event.badge);
  rc_url_builder_append_str_param(&builder, "g", achievement_event.title);
  rc_url_builder_append_num_param(&builder, "i", achievement_event.points);
  rc_url_builder_append_str_param(&builder, "f", frame_number_str);
  rc_url_builder_append_str_param(&builder, "t", time_str);
  request->request.post_data = rc_url_builder_finalize(&builder);
}

//  ---------------------------------------------------------------------------
//
//  ---------------------------------------------------------------------------
static void wc_set_keep_alive_request_url
(
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  const settings_t *settings = config_get_ptr();
  request->request.url = settings->arrays.webhook_url;

  rc_api_url_builder_t builder;
  rc_url_builder_init(&builder, &request->request.buffer, 48);

  char time_str[64];
  sprintf(time_str, "%lld", game_event.time);

  char frame_number_str[64];
  sprintf(frame_number_str, "%ld", game_event.frame_number);

  rc_url_builder_append_str_param(&builder, "h", game_event.rom_hash);
  rc_url_builder_append_num_param(&builder, "c", game_event.console_id);
  rc_url_builder_append_num_param(&builder, "e", game_event.game_event_id);
  rc_url_builder_append_str_param(&builder, "f", frame_number_str);
  rc_url_builder_append_str_param(&builder, "t", time_str);
  request->request.post_data = rc_url_builder_finalize(&builder);
}

//  ---------------------------------------------------------------------------
//  Builds and sets the request's header, mainly the bearer token.
//  ---------------------------------------------------------------------------
static void wc_set_request_header
(
  const char* access_token,
  async_http_request_t* request
)
{
  if (access_token == NULL)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to retrieve an access token\n");
    return;
  }

  const char* authorization_header = "Authorization: Bearer ";
  const size_t auth_header_len = strlen(authorization_header);
  const size_t token_len = strlen(access_token);

  //  Adds 3 because we're appending two characters and a null character at the end
  const size_t header_length = auth_header_len + token_len + 3;

  char* headers = (char*)malloc(header_length);

  if (headers == NULL) {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to allocate header\n");
    return;
  }

  strlcpy(headers, authorization_header, auth_header_len + 1);
  strlcpy(headers + auth_header_len, access_token, token_len + 1);

  headers[auth_header_len + token_len] = '\r';
  headers[auth_header_len + token_len + 1] = '\n';
  headers[auth_header_len + token_len + 2] = '\0';

  request->headers = headers;
}

//  ---------------------------------------------------------------------------
//  Configures the HTTP request to POST the progress to the webhook server.
//  ---------------------------------------------------------------------------
static void wc_prepare_progress_http_request
(
  const char* access_token,
  wc_game_event_t game_event,
  const char* progress,
  async_http_request_t* request
)
{
  wc_set_progress_request_url
  (
    game_event,
    progress,
    request
  );

  wc_set_request_header
  (
    access_token,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Prepares the HTTP request by sending its content and headers (Progress)
//  ---------------------------------------------------------------------------
static void wc_prepare_event_http_request
(
  const char* access_token,
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  wc_set_event_request_url
  (
   game_event,
   request
 );

  wc_set_request_header
  (
    access_token,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Prepares the HTTP request by sending its content and headers (Achievements)
//  ---------------------------------------------------------------------------
static void wc_prepare_achievement_http_request
(
  const char* access_token,
  wc_game_event_t game_event,
  wc_achievement_event_t achievement_event,
  async_http_request_t* request
)
{
  wc_set_achievement_request_url
  (
    game_event,
    achievement_event,
    request
  );

  wc_set_request_header
  (
    access_token,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Prepares the HTTP request by sending its content and headers (Keep Alive)
//  ---------------------------------------------------------------------------
static void wc_prepare_keep_alive_http_request
(
  const char* access_token,
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  wc_set_keep_alive_request_url
  (
    game_event,
    request
  );

  wc_set_request_header
  (
    access_token,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Builds the HTTP request and sends the progress.
//  ---------------------------------------------------------------------------
static void wc_initiate_progress_request
(
  const char* access_token,
  wc_game_event_t game_event,
  const char* progress,
  async_http_request_t* request
)
{
  wc_prepare_progress_http_request
  (
    access_token,
    game_event,
    progress,
    request
  );

  wc_begin_http_request(request);
}

//  ---------------------------------------------------------------------------
//  Builds the HTTP request and sends the game event.
//  ---------------------------------------------------------------------------
static void wc_initiate_event_request
(
  const char* access_token,
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  wc_prepare_event_http_request
  (
    access_token,
    game_event,
    request
  );

  wc_begin_http_request(request);
}

//  ---------------------------------------------------------------------------
//  Builds the HTTP request and sends the achievements.
//  ---------------------------------------------------------------------------
static void wc_initiate_achievement_request
(
  const char* access_token,
  wc_game_event_t game_event,
  wc_achievement_event_t achievement_event,
  async_http_request_t* request
)
{
  wc_prepare_achievement_http_request
  (
    access_token,
    game_event,
    achievement_event,
    request
  );

  wc_begin_http_request(request);
}

//  ---------------------------------------------------------------------------
//  Builds the HTTP request and sends the keep alive.
//  ---------------------------------------------------------------------------
static void wc_initiate_keep_alive_request
(
  const char* access_token,
  wc_game_event_t game_event,
  async_http_request_t* request
)
{
  wc_prepare_keep_alive_http_request
  (
    access_token,
    game_event,
    request
  );

  wc_begin_http_request(request);
}

//  ---------------------------------------------------------------------------
//  Sends the progress.
//  ---------------------------------------------------------------------------
void wc_update_progress
(
  const char* access_token,
  wc_game_event_t game_event,
  const char* progress
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Sending progress '%s' for ROM's hash '%s' (frame=%ld)\n", progress, game_event.rom_hash, game_event.frame_number);

  async_http_request_t *request = (async_http_request_t*) calloc(1, sizeof(async_http_request_t));

  if (!request)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to allocate HTTP request\n");
    return;
  }

  wc_initiate_progress_request
  (
    access_token,
    game_event,
    progress,
    request
   );
}

//  ---------------------------------------------------------------------------
//  Sends the game event.
//  ---------------------------------------------------------------------------
void wc_send_game_event
(
  const char* access_token,
  wc_game_event_t game_event,
  void* on_game_event_sent_callback
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Sending game event '%d' for ROM's hash '%s' (frame=%ld)\n", game_event.game_event_id, game_event.rom_hash, game_event.frame_number);

  async_http_request_t *request = (async_http_request_t*) calloc(1, sizeof(async_http_request_t));

  if (!request)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to allocate HTTP request\n");
    return;
  }

  request->handler = wc_on_game_event_sent_completed;
  
  request->callback = (async_client_callback)on_game_event_sent_callback;
  request->callback_data = (void*)&game_event;

  wc_initiate_event_request
  (
    access_token,
    game_event,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Sends the achievement information.
//  ---------------------------------------------------------------------------
void wc_send_achievement_event
(
  const char* access_token,
  wc_game_event_t game_event,
  wc_achievement_event_t achievement_event
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Sending game achievement event '%d' for ROM's hash '%s' (frame=%ld)\n", game_event.game_event_id, game_event.rom_hash, game_event.frame_number);

  async_http_request_t *request = (async_http_request_t*) calloc(1, sizeof(async_http_request_t));

  if (!request)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to allocate HTTP request for game achievement event\n");
    return;
  }

  wc_initiate_achievement_request
  (
    access_token,
    game_event,
    achievement_event,
    request
  );
}

//  ---------------------------------------------------------------------------
//  Sends a keep alive.
//  ---------------------------------------------------------------------------
void wc_send_keep_alive_event
(
  const char* access_token,
  wc_game_event_t game_event
)
{
  WEBHOOKS_LOG(WEBHOOKS_TAG "Sending keep alive event '%d' for ROM's hash '%s' (frame=%ld)\n", game_event.game_event_id, game_event.rom_hash, game_event.frame_number);

  async_http_request_t *request = (async_http_request_t*) calloc(1, sizeof(async_http_request_t));

  if (!request)
  {
    WEBHOOKS_LOG(WEBHOOKS_TAG "Failed to allocate HTTP request for keep alive event\n");
    return;
  }

  wc_initiate_keep_alive_request
  (
    access_token,
    game_event,
    request
  );
}

