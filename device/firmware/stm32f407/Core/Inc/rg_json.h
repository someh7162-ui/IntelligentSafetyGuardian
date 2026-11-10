#ifndef RG_JSON_H
#define RG_JSON_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct {int start,end,parent;char type;} rg_json_token_t;
typedef struct {const char *text;size_t length;rg_json_token_t *tokens;int count,capacity;} rg_json_t;
bool rg_json_parse(rg_json_t *,const char *,size_t,rg_json_token_t *,int);
int rg_json_get(const rg_json_t *,int,const char *);
bool rg_json_equals(const rg_json_t *,int,const char *);
bool rg_json_number(const rg_json_t *,int,double *);
bool rg_json_string(const rg_json_t *,int,char *,size_t);
#endif
