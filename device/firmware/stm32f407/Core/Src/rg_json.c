#include "rg_json.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
static void ws(const rg_json_t*j,size_t*p){while(*p<j->length&&strchr(" \t\r\n",j->text[*p]))(*p)++;}
static int value(rg_json_t*j,size_t*p,int parent,int depth){
    ws(j,p);if(depth>16||*p>=j->length||j->count>=j->capacity)return -1;
    int id=j->count++;rg_json_token_t*t=&j->tokens[id];t->parent=parent;t->start=(int)*p;t->type=j->text[*p];
    if(t->type=='{'||t->type=='['){char end=t->type=='{'?'}':']';(*p)++;ws(j,p);
        if(*p<j->length&&j->text[*p]==end){(*p)++;t->end=(int)*p;return id;}
        while(*p<j->length){
            if(t->type=='{'){ws(j,p);if(*p>=j->length||j->text[*p]!='"'||value(j,p,id,depth+1)<0)return -1;ws(j,p);if(*p>=j->length||j->text[(*p)++]!=':')return -1;}
            if(value(j,p,id,depth+1)<0)return -1;
            ws(j,p);if(*p>=j->length)return -1;char c=j->text[(*p)++];if(c==end){t->end=(int)*p;return id;}if(c!=',')return -1;
        }return -1;
    }
    if(t->type=='"'){
        (*p)++;while(*p<j->length){unsigned char c=(unsigned char)j->text[(*p)++];if(c=='"'){t->end=(int)*p;return id;}if(c<32)return -1;
            if(c=='\\'){if(*p>=j->length)return -1;c=(unsigned char)j->text[(*p)++];if(c=='u'){for(int k=0;k<4;k++)if(*p>=j->length||!isxdigit((unsigned char)j->text[(*p)++]))return -1;}else if(!strchr("\"\\/bfnrt",c))return -1;}
        }return -1;
    }
    size_t start=*p;while(*p<j->length&&!strchr(",]} \r\n\t",j->text[*p]))(*p)++;
    size_t n=*p-start;t->end=(int)*p;t->type='p';
    if((n==4&&!memcmp(j->text+start,"true",4))||(n==5&&!memcmp(j->text+start,"false",5))||(n==4&&!memcmp(j->text+start,"null",4)))return id;
    size_t k=start;if(k<*p&&j->text[k]=='-')k++;
    if(k>=*p)return -1;
    if(j->text[k]=='0')k++;else{if(j->text[k]<'1'||j->text[k]>'9')return -1;while(k<*p&&isdigit((unsigned char)j->text[k]))k++;}
    if(k<*p&&j->text[k]=='.'){k++;size_t b=k;while(k<*p&&isdigit((unsigned char)j->text[k]))k++;if(k==b)return -1;}
    if(k<*p&&(j->text[k]=='e'||j->text[k]=='E')){k++;if(k<*p&&(j->text[k]=='+'||j->text[k]=='-'))k++;size_t b=k;while(k<*p&&isdigit((unsigned char)j->text[k]))k++;if(k==b)return -1;}
    return k==*p?id:-1;
}
bool rg_json_parse(rg_json_t*j,const char*text,size_t n,rg_json_token_t*t,int capacity){
    if(!j||!text||!t||capacity<1||n>16384)return false;
    *j=(rg_json_t){text,n,t,0,capacity};size_t p=0;if(value(j,&p,-1,0)<0)return false;ws(j,&p);return p==n;
}
bool rg_json_equals(const rg_json_t*j,int i,const char*s){if(i<0||i>=j->count)return false;rg_json_token_t t=j->tokens[i];int a=t.start,b=t.end;if(t.type=='"'){a++;b--;}return (size_t)(b-a)==strlen(s)&&!memcmp(j->text+a,s,(size_t)(b-a));}
int rg_json_get(const rg_json_t*j,int obj,const char*key){
    if(obj<0||obj>=j->count||j->tokens[obj].type!='{')return -1;
    int found=-1;
    for(int i=obj+1;i+1<j->count&&j->tokens[i].start<j->tokens[obj].end;i++)if(j->tokens[i].parent==obj&&j->tokens[i].type=='"'){
        int v=i+1;if(rg_json_equals(j,i,key)){if(found>=0)return -1;found=v;}
        while(i+1<j->count&&j->tokens[i+1].start<j->tokens[v].end)i++;
    }
    return found;
}
bool rg_json_number(const rg_json_t*j,int i,double*out){
    if(i<0||i>=j->count||j->tokens[i].type!='p'||rg_json_equals(j,i,"null")||rg_json_equals(j,i,"true")||rg_json_equals(j,i,"false"))return false;
    int n=j->tokens[i].end-j->tokens[i].start;char b[48];if(n<1||n>46)return false;memcpy(b,j->text+j->tokens[i].start,(size_t)n);b[n]=0;char*end;double v=strtod(b,&end);if(*end||!isfinite(v))return false;*out=v;return true;
}
bool rg_json_string(const rg_json_t*j,int i,char*out,size_t cap){if(i<0||i>=j->count||j->tokens[i].type!='"')return false;int n=j->tokens[i].end-j->tokens[i].start-2;if(n<0||(size_t)n>=cap||memchr(j->text+j->tokens[i].start+1,'\\',(size_t)n))return false;memcpy(out,j->text+j->tokens[i].start+1,(size_t)n);out[n]=0;return true;}
