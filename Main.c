/*
 * NHP COMPILER v1.1 — NHP -> x86-64 NASM
 * FIXED: segfault (heap buffer), no sys() calls by default
 * COMPILE: gcc -O2 -o nhpc nhp_compiler.c
 * Termux:  gcc -O2 -o nhpc nhp_compiler.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define NHP_VERSION   "1.1"
#define MAX_TOKENS    8192
#define MAX_IDENT     256
#define MAX_STR       1024
#define MAX_CHILDREN  64

typedef enum {
    TOK_PSET,TOK_BYTES,TOK_LEN,TOK_BEGIN,TOK_DOLLAR,
    TOK_MATHS,TOK_REG,TOK_REMP,TOK_EXECUTE,
    TOK_IMP,TOK_ADD,TOK_SUB,TOK_MUL,TOK_DIV,
    TOK_CLS,TOK_JNS,TOK_PRINT,TOK_ALIAS,
    TOK_SCR,TOK_SCT,TOK_SCX,TOK_SCZ,
    TOK_BTN,TOK_TEXTBOX,TOK_OUTPUT_KW,
    TOK_MOUSE_CLICK,TOK_CUSTOMERS_IMP,TOK_CUSTOMER_NAME,
    TOK_LLTD,TOK_PIC_URL,TOK_ASM_BLOCK,
    TOK_IDENT,TOK_NUMBER,TOK_STRING,TOK_CHAR,TOK_LABEL,
    TOK_LBRACKET,TOK_RBRACKET,TOK_LPAREN,TOK_RPAREN,
    TOK_LBRACE,TOK_RBRACE,TOK_COMMA,TOK_EQUALS,
    TOK_MINUS,TOK_STAR,TOK_COLON,TOK_HASH,
    TOK_EOF,TOK_UNKNOWN
} TT;

static const char *TTNAME[]={
    "PSET","BYTES","LEN","BEGIN","DOLLAR","MATHS","REG","REMP","EXECUTE",
    "IMP","ADD","SUB","MUL","DIV","CLS","JNS","PRINT","ALIAS",
    "SCR","SCT","SCX","SCZ","BTN","TEXTBOX","OUTPUT","MOUSE_CLICK",
    "CUSTOMERS_IMP","CUSTOMER_NAME","LLTD","PIC_URL","ASM_BLOCK",
    "IDENT","NUMBER","STRING","CHAR","LABEL",
    "[","]","(",")","{"," }",",","=","-","*",":","#","EOF","?"
};

typedef struct { TT type; char value[MAX_STR]; int line; } Token;

typedef enum {
    N_PROG,N_PSET,N_BYTES,N_MATHS,N_REG,N_EXEC,
    N_IMP,N_ADD,N_SUB,N_MUL,N_DIV,N_CLS,N_JNS,
    N_LABEL,N_PRINT,N_ALIAS,N_BTN,N_TEXTBOX,N_OUTPUT,
    N_CLICK,N_CUSTNAME,N_PICURL,N_ASM,
    N_NUM,N_STR,N_IDENT,N_REG2
} NT;

typedef struct AN { NT type; char v[MAX_STR],v2[MAX_STR],v3[MAX_STR]; int ival,line,nch; struct AN *ch[MAX_CHILDREN]; } AN;

/* ── dynamic output buffer on heap ── */
typedef struct { char *buf; int len,cap; } OB;
static void ob_init(OB *b){ b->cap=65536; b->buf=(char*)malloc(b->cap); b->buf[0]=0; b->len=0; }
static void ob_free(OB *b){ free(b->buf); }
static void ob_grow(OB *b, int need){
    while(b->len+need+2 >= b->cap){ b->cap*=2; b->buf=(char*)realloc(b->buf,b->cap); }
}
static void ob_w(OB *b, const char *fmt,...){
    va_list ap; ob_grow(b,512);
    va_start(ap,fmt); int n=vsnprintf(b->buf+b->len,b->cap-b->len,fmt,ap); va_end(ap);
    if(n>0){ if(b->len+n>=b->cap){ ob_grow(b,n+1); va_start(ap,fmt); n=vsnprintf(b->buf+b->len,b->cap-b->len,fmt,ap); va_end(ap); } b->len+=n; }
}
static void ob_l(OB *b, const char *fmt,...){
    va_list ap; ob_grow(b,512);
    va_start(ap,fmt); int n=vsnprintf(b->buf+b->len,b->cap-b->len,fmt,ap); va_end(ap);
    if(n>0){ if(b->len+n>=b->cap){ ob_grow(b,n+1); va_start(ap,fmt); n=vsnprintf(b->buf+b->len,b->cap-b->len,fmt,ap); va_end(ap); } b->len+=n; }
    b->buf[b->len++]='\n'; b->buf[b->len]=0;
}

/* ── lexer ── */
typedef struct { const char *src; int pos,line; Token *toks; int cnt,cap; } LX;
static void lx_init(LX *l, const char *s){ l->src=s;l->pos=0;l->line=1;l->cap=1024;l->cnt=0;l->toks=(Token*)malloc(l->cap*sizeof(Token)); }
static void lx_free(LX *l){ free(l->toks); }
static char lp(LX *l){ return l->src[l->pos]; }
static char lp2(LX *l){ return l->src[l->pos]?l->src[l->pos+1]:0; }
static char la(LX *l){ char c=l->src[l->pos++]; if(c=='\n')l->line++; return c; }
static void lpush(LX *l, TT t, const char *v){
    if(l->cnt>=l->cap){l->cap*=2;l->toks=(Token*)realloc(l->toks,l->cap*sizeof(Token));}
    l->toks[l->cnt].type=t; l->toks[l->cnt].line=l->line;
    strncpy(l->toks[l->cnt].value,v?v:"",MAX_STR-1); l->cnt++;
}
typedef struct { const char *w; TT t; } KW;
static KW KWS[]={
    {"Pset",TOK_PSET},{"Maths",TOK_MATHS},{"Reg",TOK_REG},{"Remp",TOK_REMP},
    {"Execute_system",TOK_EXECUTE},{"IMP",TOK_IMP},{"Add",TOK_ADD},{"Sub",TOK_SUB},
    {"Mul",TOK_MUL},{"Div",TOK_DIV},{"cls",TOK_CLS},{"jns",TOK_JNS},
    {"Print",TOK_PRINT},{"Alias",TOK_ALIAS},{"Alieas",TOK_ALIAS},
    {"SCR",TOK_SCR},{"SCT",TOK_SCT},{"SCX",TOK_SCX},{"SCZ",TOK_SCZ},
    {"Textbox",TOK_TEXTBOX},{"Output",TOK_OUTPUT_KW},{"Mouse2Click",TOK_MOUSE_CLICK},
    {"Customers_IMP",TOK_CUSTOMERS_IMP},{"CustomerName",TOK_CUSTOMER_NAME},
    {"LLtd",TOK_LLTD},{NULL,TOK_UNKNOWN}
};
static void lx_run(LX *l){
    while(lp(l)){
        while(lp(l)&&(lp(l)==' '||lp(l)=='\t'||lp(l)=='\r'||lp(l)=='\n')) la(l);
        if(!lp(l)) break;
        char c=lp(l);
        /* comments */
        if(c==';'){while(lp(l)&&lp(l)!='\n')la(l);continue;}
        if(c=='/'&&lp2(l)=='/'){while(lp(l)&&lp(l)!='\n')la(l);continue;}
        if(c=='/'&&lp2(l)=='*'){la(l);la(l);while(lp(l)){if(lp(l)=='*'&&lp2(l)=='/'){la(l);la(l);break;}la(l);}continue;}
        /* multi-char */
        const char *s=&l->src[l->pos];
        if(strncmp(s,"====Asm",7)==0){
            int i;for(i=0;i<7;i++)la(l);
            lpush(l,TOK_ASM_BLOCK,"====Asm");
            while(lp(l)&&lp(l)!='{')la(l);
            if(!lp(l))continue;
            la(l);
            int cap=4096; char *buf=(char*)malloc(cap); int bi=0,depth=1;
            while(lp(l)&&depth>0){
                char cc=lp(l);
                if(cc=='{'&&depth>0)depth++;
                if(cc=='}'){depth--;if(depth==0){la(l);break;}}
                if(bi>=cap-2){cap*=2;buf=(char*)realloc(buf,cap);}
                buf[bi++]=la(l);
            }
            buf[bi]=0; lpush(l,TOK_ASM_BLOCK,buf); free(buf); continue;
        }
        if(strncmp(s,"_begin",6)==0&&!isalnum((unsigned char)s[6])&&s[6]!='_'){int i;for(i=0;i<6;i++)la(l);if(lp(l)==':')la(l);lpush(l,TOK_BEGIN,"_begin");continue;}
        if(strncmp(s,"_len",4)==0&&!isalnum((unsigned char)s[4])&&s[4]!='_'){int i;for(i=0;i<4;i++)la(l);lpush(l,TOK_LEN,"_len");continue;}
        if(strncmp(s,".bytes",6)==0){int i;for(i=0;i<6;i++)la(l);lpush(l,TOK_BYTES,".bytes");continue;}
        if(strncmp(s,"[Btn]",5)==0){int i;for(i=0;i<5;i++)la(l);lpush(l,TOK_BTN,"Btn");continue;}
        /* string */
        if(c=='"'){la(l);char buf[MAX_STR]={0};int bi=0;while(lp(l)&&lp(l)!='"'){if(lp(l)=='\\'){la(l);char e=la(l);switch(e){case 'n':buf[bi++]='\n';break;case 't':buf[bi++]='\t';break;default:buf[bi++]=e;}}else if(bi<MAX_STR-1)buf[bi++]=la(l);else la(l);}if(lp(l)=='"')la(l);lpush(l,TOK_STRING,buf);continue;}
        /* char */
        if(c=='\''){la(l);char buf[4]={0};if(lp(l)=='\\'){la(l);char e=la(l);switch(e){case 'n':buf[0]='\n';break;case 't':buf[0]='\t';break;default:buf[0]=e;}}else buf[0]=la(l);if(lp(l)=='\'')la(l);lpush(l,TOK_CHAR,buf);continue;}
        /* number */
        if(isdigit((unsigned char)c)){char buf[64]={0};int bi=0;if(c=='0'&&(lp2(l)=='x'||lp2(l)=='X')){buf[bi++]=la(l);buf[bi++]=la(l);while(isxdigit((unsigned char)lp(l))&&bi<62)buf[bi++]=la(l);}else{while(isdigit((unsigned char)lp(l))&&bi<62)buf[bi++]=la(l);}lpush(l,TOK_NUMBER,buf);continue;}
        /* ident/kw */
        if(isalpha((unsigned char)c)||c=='_'||c=='.'){
            char buf[MAX_IDENT]={0};int bi=0;
            while(lp(l)&&(isalnum((unsigned char)lp(l))||lp(l)=='_'||lp(l)=='.')&&bi<MAX_IDENT-1)buf[bi++]=la(l);
            if(lp(l)==':'){la(l);lpush(l,TOK_LABEL,buf);continue;}
            int k;for(k=0;KWS[k].w;k++){if(strcmp(buf,KWS[k].w)==0){lpush(l,KWS[k].t,buf);goto done;}}
            lpush(l,TOK_IDENT,buf);done:continue;
        }
        la(l);
        switch(c){
            case '[':lpush(l,TOK_LBRACKET,"[");break;case ']':lpush(l,TOK_RBRACKET,"]");break;
            case '(':lpush(l,TOK_LPAREN,"(");break;case ')':lpush(l,TOK_RPAREN,")");break;
            case '{':lpush(l,TOK_LBRACE,"{");break;case '}':lpush(l,TOK_RBRACE,"}");break;
            case ',':lpush(l,TOK_COMMA,",");break;case '=':lpush(l,TOK_EQUALS,"=");break;
            case '-':lpush(l,TOK_MINUS,"-");break;case '*':lpush(l,TOK_STAR,"*");break;
            case ':':lpush(l,TOK_COLON,":");break;case '#':lpush(l,TOK_HASH,"#");break;
            case '$':lpush(l,TOK_DOLLAR,"$");break;default:break;
        }
    }
    lpush(l,TOK_EOF,"");
}

/* ── parser ── */
typedef struct { Token *t; int pos,cnt; } PR;
static Token *pc(PR *p){return &p->t[p->pos];}
static Token *pe(PR *p){return &p->t[p->pos++];}
static int pa(PR *p,TT t){return pc(p)->type==t;}
static void pm(PR *p,TT t){if(pa(p,t))pe(p);}
static AN *an(NT t,int line){AN *n=(AN*)calloc(1,sizeof(AN));n->type=t;n->line=line;return n;}
static void ac(AN *p,AN *c){if(p&&c&&p->nch<MAX_CHILDREN)p->ch[p->nch++]=c;}
static void afree(AN *n){if(!n)return;int i;for(i=0;i<n->nch;i++)afree(n->ch[i]);free(n);}

static AN *pop2(PR *p){
    AN *n=NULL; int ln=pc(p)->line;
    switch(pc(p)->type){
        case TOK_SCR:case TOK_SCT:case TOK_SCX:case TOK_SCZ:
            n=an(N_REG2,ln);strcpy(n->v,pe(p)->value);break;
        case TOK_NUMBER:{Token *t=pe(p);n=an(N_NUM,ln);strcpy(n->v,t->value);n->ival=(int)strtol(t->value,NULL,0);break;}
        case TOK_CHAR:{Token *t=pe(p);n=an(N_NUM,ln);n->ival=(unsigned char)t->value[0];snprintf(n->v,MAX_STR,"%d",n->ival);break;}
        case TOK_STRING:n=an(N_STR,ln);strcpy(n->v,pe(p)->value);break;
        case TOK_IDENT:case TOK_LABEL:n=an(N_IDENT,ln);strcpy(n->v,pe(p)->value);break;
        case TOK_MINUS:pe(p);n=an(N_NUM,ln);if(pa(p,TOK_NUMBER)){Token *t=pe(p);n->ival=-(int)strtol(t->value,NULL,0);snprintf(n->v,MAX_STR,"%d",n->ival);}break;
        default:pe(p);break;
    }
    return n;
}
static AN *p2op(PR *p,NT nt){int ln=pc(p)->line;pe(p);AN *n=an(nt,ln);AN *a=pop2(p);if(a)ac(n,a);pm(p,TOK_COMMA);AN *b=pop2(p);if(b)ac(n,b);return n;}
static AN *pinstr(PR *p);
static void pbody(PR *p,AN *s){int br=0;if(pa(p,TOK_LBRACE)){pe(p);br=1;}while(!pa(p,TOK_EOF)){if(br&&pa(p,TOK_RBRACE)){pe(p);break;}if(!br&&(pa(p,TOK_PSET)||pa(p,TOK_MATHS)||pa(p,TOK_REG)||pa(p,TOK_EXECUTE)))break;AN *c=pinstr(p);if(c)ac(s,c);}}
static AN *pinstr(PR *p){
    int ln=pc(p)->line;
    switch(pc(p)->type){
        case TOK_IMP:return p2op(p,N_IMP);case TOK_ADD:return p2op(p,N_ADD);
        case TOK_SUB:return p2op(p,N_SUB);case TOK_MUL:return p2op(p,N_MUL);
        case TOK_DIV:return p2op(p,N_DIV);case TOK_CLS:return p2op(p,N_CLS);
        case TOK_JNS:{pe(p);AN *n=an(N_JNS,ln);if(pa(p,TOK_IDENT)||pa(p,TOK_LABEL))strcpy(n->v,pe(p)->value);return n;}
        case TOK_PRINT:{pe(p);AN *n=an(N_PRINT,ln);if(pa(p,TOK_STRING)||pa(p,TOK_IDENT))strcpy(n->v,pe(p)->value);return n;}
        case TOK_BEGIN:case TOK_LABEL:{AN *n=an(N_LABEL,ln);strcpy(n->v,pe(p)->value);return n;}
        case TOK_ASM_BLOCK:{pe(p);AN *n=an(N_ASM,ln);if(pa(p,TOK_ASM_BLOCK))strcpy(n->v,pe(p)->value);return n;}
        case TOK_ALIAS:{pe(p);AN *n=an(N_ALIAS,ln);if(pa(p,TOK_IDENT))strcpy(n->v,pe(p)->value);pm(p,TOK_EQUALS);if(!pa(p,TOK_EOF))strcpy(n->v2,pe(p)->value);return n;}
        case TOK_BTN:{pe(p);AN *n=an(N_BTN,ln);while(pa(p,TOK_IDENT)){Token *pr=pe(p);pm(p,TOK_EQUALS);if(pa(p,TOK_STRING)){char *v=pe(p)->value;if(strstr(pr->value,"Color"))strncpy(n->v,v,MAX_STR-1);else if(strstr(pr->value,"Text"))strncpy(n->v2,v,MAX_STR-1);else if(strstr(pr->value,"Msp"))strncpy(n->v3,v,MAX_STR-1);}else if(pa(p,TOK_NUMBER))pe(p);}return n;}
        case TOK_TEXTBOX:{pe(p);AN *n=an(N_TEXTBOX,ln);if(pa(p,TOK_STRING)||pa(p,TOK_IDENT))strcpy(n->v,pe(p)->value);return n;}
        case TOK_OUTPUT_KW:{pe(p);AN *n=an(N_OUTPUT,ln);if(pa(p,TOK_STRING)||pa(p,TOK_IDENT))strcpy(n->v,pe(p)->value);return n;}
        case TOK_MOUSE_CLICK:{pe(p);AN *n=an(N_CLICK,ln);pm(p,TOK_LPAREN);if(pa(p,TOK_LBRACE)){pe(p);while(!pa(p,TOK_RBRACE)&&!pa(p,TOK_EOF)){AN *c=pinstr(p);if(c)ac(n,c);}pm(p,TOK_RBRACE);}return n;}
        case TOK_CUSTOMER_NAME:{pe(p);AN *n=an(N_CUSTNAME,ln);pm(p,TOK_EQUALS);if(pa(p,TOK_STRING))strcpy(n->v,pe(p)->value);return n;}
        case TOK_LLTD:{pe(p);AN *n=an(N_PICURL,ln);while(!pa(p,TOK_EOF)&&!pa(p,TOK_PSET)&&!pa(p,TOK_MATHS)&&!pa(p,TOK_EXECUTE)){if(pa(p,TOK_STRING)){strcpy(n->v,pe(p)->value);break;}pe(p);}return n;}
        default:pe(p);return NULL;
    }
}
static AN *parse_prog(LX *lx){
    PR p; p.t=lx->toks; p.cnt=lx->cnt; p.pos=0;
    AN *root=an(N_PROG,0); strcpy(root->v,"program");
    while(!pa(&p,TOK_EOF)){
        AN *n=NULL; int ln=pc(&p)->line;
        switch(pc(&p)->type){
            case TOK_PSET:{pe(&p);n=an(N_PSET,ln);if(pa(&p,TOK_STRING))strcpy(n->v,pe(&p)->value);else if(pa(&p,TOK_IDENT))strcpy(n->v,pe(&p)->value);if(pa(&p,TOK_LEN)){pe(&p);strcpy(n->v2,"_len");}if(pa(&p,TOK_MINUS)){pe(&p);if(pa(&p,TOK_IDENT))pe(&p);}if(pa(&p,TOK_IDENT)){Token *nm=pe(&p);pm(&p,TOK_EQUALS);if(pa(&p,TOK_LBRACKET)){pe(&p);if(pa(&p,TOK_NUMBER))n->ival=atoi(pe(&p)->value);if(pa(&p,TOK_COMMA))pe(&p);if(pa(&p,TOK_NUMBER))pe(&p);pm(&p,TOK_RBRACKET);}strncpy(n->v3,nm->value,MAX_STR-1);}if(pa(&p,TOK_BYTES)){pe(&p);AN *bd=an(N_BYTES,ln);while(pa(&p,TOK_CHAR)||pa(&p,TOK_NUMBER)||pa(&p,TOK_STRING)){AN *b=pop2(&p);if(b)ac(bd,b);}ac(n,bd);}if(pa(&p,TOK_BEGIN)||pa(&p,TOK_LABEL)){AN *lbl=an(N_LABEL,pc(&p)->line);strcpy(lbl->v,pe(&p)->value);ac(n,lbl);}break;}
            case TOK_MATHS:{pe(&p);n=an(N_MATHS,ln);if(pa(&p,TOK_STAR))pe(&p);if(pa(&p,TOK_IDENT)||pa(&p,TOK_NUMBER))strcpy(n->v,pe(&p)->value);if(pa(&p,TOK_LPAREN)){pe(&p);while(!pa(&p,TOK_RPAREN)&&!pa(&p,TOK_EOF))pe(&p);pm(&p,TOK_RPAREN);}pbody(&p,n);break;}
            case TOK_REG:{pe(&p);n=an(N_REG,ln);pm(&p,TOK_REMP);if(pa(&p,TOK_NUMBER)){n->ival=atoi(pc(&p)->value);pe(&p);}pbody(&p,n);break;}
            case TOK_EXECUTE:{pe(&p);n=an(N_EXEC,ln);if(pa(&p,TOK_IDENT))strcpy(n->v,pe(&p)->value);pbody(&p,n);break;}
            default:n=pinstr(&p);break;
        }
        if(n)ac(root,n);
    }
    return root;
}

/* ── codegen ── */
static const char *r64(const char *r){if(!strcmp(r,"SCR"))return "rax";if(!strcmp(r,"SCT"))return "rbx";if(!strcmp(r,"SCX"))return "rcx";if(!strcmp(r,"SCZ"))return "rdx";return r;}
static void opstr(AN *op,char *buf,int sz){if(!op){buf[0]=0;return;}switch(op->type){case N_REG2:snprintf(buf,sz,"%s",r64(op->v));break;case N_NUM:snprintf(buf,sz,"%d",op->ival);break;default:snprintf(buf,sz,"%s",op->v);break;}}
static void tolbl(const char *in,char *out,int sz){int i=0,j=0;while(in[i]&&j<sz-1){char c=in[i++];out[j++]=(isalnum((unsigned char)c)||c=='_')?c:'_';}out[j]=0;}
static int lbl_ctr=0;
static void gen_print(OB *ob,const char *lbl){char l[MAX_IDENT];tolbl(lbl,l,sizeof(l));ob_l(ob,"    ; Print '%s'",lbl);ob_l(ob,"    mov  rax, 1");ob_l(ob,"    mov  rdi, 1");ob_l(ob,"    mov  rsi, %s",l);ob_l(ob,"    mov  rdx, %s_len",l);ob_l(ob,"    syscall");}
static void gen_node(OB *ob,AN *n){
    if(!n)return; char a[128]={0},b[128]={0},lbl[MAX_IDENT]={0}; int i;
    switch(n->type){
        case N_IMP:if(n->nch>=1)opstr(n->ch[0],a,128);if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    mov  %s, %s",a,b);break;
        case N_ADD:if(n->nch>=1)opstr(n->ch[0],a,128);if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    add  %s, %s",a,b);break;
        case N_SUB:if(n->nch>=1)opstr(n->ch[0],a,128);if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    sub  %s, %s",a,b);break;
        case N_MUL:if(n->nch>=1)opstr(n->ch[0],a,128);if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    imul %s, %s",a,b);break;
        case N_DIV:if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    xor  rdx, rdx");ob_l(ob,"    idiv %s",b);break;
        case N_CLS:if(n->nch>=1)opstr(n->ch[0],a,128);if(n->nch>=2)opstr(n->ch[1],b,128);ob_l(ob,"    cmp  %s, %s",a,b);break;
        case N_JNS:tolbl(n->v,lbl,sizeof(lbl));ob_l(ob,"    jns  %s",lbl);break;
        case N_LABEL:tolbl(n->v,lbl,sizeof(lbl));ob_l(ob,"%s:",lbl);break;
        case N_PRINT:if(n->v[0])gen_print(ob,n->v);break;
        case N_ALIAS:ob_l(ob,"    ; alias %s = %s",n->v,n->v2);break;
        case N_ASM:ob_l(ob,"    ; --- inline asm ---");ob_l(ob,"%s",n->v);ob_l(ob,"    ; --- end inline asm ---");break;
        case N_BTN:ob_l(ob,"_btn_%d:",lbl_ctr++);ob_l(ob,"    ; [Btn] text='%s' color='%s'",n->v2[0]?n->v2:"Button",n->v[0]?n->v:"default");break;
        case N_TEXTBOX:ob_l(ob,"    ; Textbox '%s'",n->v);ob_l(ob,"    mov  rax, 0");ob_l(ob,"    mov  rdi, 0");ob_l(ob,"    mov  rsi, _textbox_buf");ob_l(ob,"    mov  rdx, 256");ob_l(ob,"    syscall");break;
        case N_OUTPUT:ob_l(ob,"    ; Output '%s'",n->v);ob_l(ob,"    mov  rax, 1");ob_l(ob,"    mov  rdi, 1");ob_l(ob,"    mov  rsi, _textbox_buf");ob_l(ob,"    mov  rdx, 256");ob_l(ob,"    syscall");break;
        case N_CLICK:ob_l(ob,"_onclick_%d:",lbl_ctr);for(i=0;i<n->nch;i++)gen_node(ob,n->ch[i]);ob_l(ob,"    ret");lbl_ctr++;break;
        case N_CUSTNAME:ob_l(ob,"    ; CustomerName='%s'",n->v);ob_l(ob,"    mov  rax, 1");ob_l(ob,"    mov  rdi, 1");ob_l(ob,"    mov  rsi, customer_name");ob_l(ob,"    mov  rdx, customer_name_len");ob_l(ob,"    syscall");break;
        case N_PICURL:ob_l(ob,"    ; Pic.URL '%s'",n->v);break;
        case N_MATHS:ob_l(ob,"    ; === Maths ===");for(i=0;i<n->nch;i++)gen_node(ob,n->ch[i]);ob_l(ob,"    ; === end Maths ===");break;
        case N_REG:ob_l(ob,"    ; === Reg (Remp %d) ===",n->ival);for(i=0;i<n->nch;i++)gen_node(ob,n->ch[i]);ob_l(ob,"    ; === end Reg ===");break;
        case N_EXEC:ob_l(ob,"    ; === Execute_system: %s ===",n->v);for(i=0;i<n->nch;i++)gen_node(ob,n->ch[i]);ob_l(ob,"    ; === end Execute_system ===");break;
        case N_PSET:break;
        default:break;
    }
}
static void gen_data(OB *ob,AN *root){
    int i,j,k; ob_l(ob,"section .data");
    for(i=0;i<root->nch;i++){
        AN *n=root->ch[i];
        if(n->type==N_PSET){
            char lbl[MAX_IDENT]; tolbl(n->v3[0]?n->v3:n->v,lbl,sizeof(lbl));
            if(!lbl[0]||lbl[0]=='_')snprintf(lbl,sizeof(lbl),"str_%d",i);
            int hb=0; for(j=0;j<n->nch;j++)if(n->ch[j]->type==N_BYTES)hb=1;
            if(hb){for(j=0;j<n->nch;j++){AN *bd=n->ch[j];if(bd->type!=N_BYTES)continue;ob_w(ob,"    %s db ",lbl);for(k=0;k<bd->nch;k++){if(k)ob_w(ob,",");int v=bd->ch[k]->ival?(int)bd->ch[k]->ival:(unsigned char)bd->ch[k]->v[0];ob_w(ob,"%d",v);}ob_w(ob,",0\n");ob_l(ob,"    %s_len equ $ - %s",lbl,lbl);}}
            else{ob_l(ob,"    %s db '%s',0",lbl,n->v);ob_l(ob,"    %s_len equ $ - %s",lbl,lbl);}
        }
        if(n->type==N_CUSTNAME&&n->v[0]){ob_l(ob,"    customer_name db '%s',0",n->v);ob_l(ob,"    customer_name_len equ $ - customer_name");}
    }
    ob_l(ob,"    _newline db 10"); ob_l(ob,"");
}
static void codegen(OB *ob,AN *root){
    int i; lbl_ctr=0;
    ob_l(ob,"; ============================================");
    ob_l(ob,"; NHP Compiler v%s  --  NHP -> NASM x86-64",NHP_VERSION);
    ob_l(ob,"; nasm -f elf64 out.asm -o out.o");
    ob_l(ob,"; ld out.o -o out  &&  ./out");
    ob_l(ob,"; ============================================");
    ob_l(ob,""); ob_l(ob,"global _start"); ob_l(ob,"");
    gen_data(ob,root);
    ob_l(ob,"section .bss"); ob_l(ob,"    _textbox_buf resb 256"); ob_l(ob,"");
    ob_l(ob,"section .text"); ob_l(ob,""); ob_l(ob,"_start:");
    for(i=0;i<root->nch;i++){AN *n=root->ch[i];if(n->type!=N_PSET&&n->type!=N_CUSTNAME)gen_node(ob,n);}
    ob_l(ob,""); ob_l(ob,"    ; exit(0)"); ob_l(ob,"    mov  rax, 60"); ob_l(ob,"    xor  rdi, rdi"); ob_l(ob,"    syscall");
}

/* ── hex dump ── */
static void hex_dump(const char *data,int len){
    printf("\n=== NHP HEX DUMP  (%d bytes) ===\n",len);
    int i;
    for(i=0;i<len;i+=16){
        printf("  %08x  ",i);
        int j;
        for(j=0;j<16;j++){if(i+j<len)printf("%02x ",(unsigned char)data[i+j]);else printf("   ");if(j==7)printf(" ");}
        printf(" |");
        for(j=0;j<16&&i+j<len;j++){unsigned char c=(unsigned char)data[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("|\n");
    }
    printf("=== END HEX DUMP ===\n\n");
}

/* ── ast dump ── */
static const char *nname(NT t){switch(t){case N_PROG:return"PROGRAM";case N_PSET:return"PSET";case N_BYTES:return"BYTES";case N_MATHS:return"MATHS";case N_REG:return"REG";case N_EXEC:return"EXECUTE";case N_IMP:return"IMP(mov)";case N_ADD:return"ADD";case N_SUB:return"SUB";case N_MUL:return"MUL";case N_DIV:return"DIV";case N_CLS:return"CLS(cmp)";case N_JNS:return"JNS(jns)";case N_LABEL:return"LABEL";case N_PRINT:return"PRINT";case N_ALIAS:return"ALIAS";case N_BTN:return"BTN";case N_TEXTBOX:return"TEXTBOX";case N_OUTPUT:return"OUTPUT";case N_CLICK:return"MOUSE_CLICK";case N_CUSTNAME:return"CUSTOMER_NAME";case N_PICURL:return"PIC_URL";case N_ASM:return"ASM_INLINE";case N_NUM:return"NUMBER";case N_STR:return"STRING";case N_IDENT:return"IDENT";case N_REG2:return"REGISTER";default:return"?";}}
static void adump(AN *n,int d){if(!n)return;int i;for(i=0;i<d*2;i++)putchar(' ');printf("[%s]",nname(n->type));if(n->v[0])printf(" v='%s'",n->v);if(n->v2[0])printf(" v2='%s'",n->v2);if(n->ival)printf(" int=%d",n->ival);printf(" L%d\n",n->line);for(i=0;i<n->nch;i++)adump(n->ch[i],d+1);}

/* ── main ── */
static char *readfile(const char *path){FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"[NHP] Cannot open: %s\n",path);return NULL;}fseek(f,0,SEEK_END);long sz=ftell(f);rewind(f);char *buf=(char*)malloc((size_t)sz+1);if(!buf){fclose(f);return NULL;}size_t rd=fread(buf,1,(size_t)sz,f);buf[rd]=0;fclose(f);return buf;}

int main(int argc,char **argv){
    printf("  _   _ _   _ ____\n");
    printf(" | \\ | | | | |  _ \\\n");
    printf(" |  \\| | |_| | |_) |\n");
    printf(" | |\\  |  _  |  __/\n");
    printf(" |_| \\_|_| |_|_|\n");
    printf("\n  NHP Compiler v%s  |  NHP -> x86-64 NASM\n",NHP_VERSION);
    printf("  SCR=rax SCT=rbx SCX=rcx SCZ=rdx\n\n");

    if(argc<2){printf("Usage: %s <file.nhp> [-o out.asm] [-tokens] [-ast] [-hex] [-run] [-v]\n",argv[0]);return 1;}

    const char *src_file=NULL,*out_file="out.asm";
    int do_tok=0,do_ast=0,do_hex=0,do_run=0,verbose=0,i;
    for(i=1;i<argc;i++){
        if(!strcmp(argv[i],"-o")&&i+1<argc)out_file=argv[++i];
        else if(!strcmp(argv[i],"-tokens"))do_tok=1;
        else if(!strcmp(argv[i],"-ast"))do_ast=1;
        else if(!strcmp(argv[i],"-hex"))do_hex=1;
        else if(!strcmp(argv[i],"-run"))do_run=1;
        else if(!strcmp(argv[i],"-v"))verbose=1;
        else if(!strcmp(argv[i],"-h")){printf("Usage: %s <file.nhp> [-o out] [-tokens] [-ast] [-hex] [-run] [-v]\nTermux install: pkg install nasm binutils\n",argv[0]);return 0;}
        else if(argv[i][0]!='-')src_file=argv[i];
    }
    if(!src_file){fprintf(stderr,"[NHP] No source file.\n");return 1;}

    char *source=readfile(src_file);
    if(!source)return 1;

    LX lx; lx_init(&lx,source); lx_run(&lx);
    if(verbose)printf("[NHP] Tokens: %d\n",lx.cnt);
    if(do_tok){
        printf("\n=== NHP TOKEN DUMP ===\n");
        for(i=0;i<lx.cnt;i++){Token *t=&lx.toks[i];int idx=(int)t->type;printf("  [%4d] L%-4d  %-20s  '%s'\n",i,t->line,(idx>=0&&idx<48)?TTNAME[idx]:"?",t->value);}
        printf("=== %d tokens ===\n",lx.cnt);
        lx_free(&lx);free(source);return 0;
    }

    AN *ast=parse_prog(&lx);
    if(verbose)printf("[NHP] AST nodes: %d\n",ast->nch);
    if(do_ast){printf("\n=== NHP AST ===\n");adump(ast,0);printf("=== END AST ===\n");afree(ast);lx_free(&lx);free(source);return 0;}

    OB ob; ob_init(&ob);
    codegen(&ob,ast);
    if(verbose)printf("[NHP] Output: %d bytes\n",ob.len);

    FILE *out=fopen(out_file,"w");
    if(!out){fprintf(stderr,"[NHP] Cannot write: %s\n",out_file);ob_free(&ob);afree(ast);lx_free(&lx);free(source);return 1;}
    fputs(ob.buf,out);fclose(out);
    printf("[NHP] OK  %s  ->  %s  (%d bytes)\n",src_file,out_file,ob.len);

    if(do_hex) hex_dump(ob.buf,ob.len);

    if(do_run){
#if defined(_WIN32)||defined(__ANDROID__)
        fprintf(stderr,"[NHP] -run needs Linux + nasm + ld\nTermux: pkg install nasm binutils\n");
#else
        char cmd[512];
        system("which nasm >/dev/null 2>&1 || (echo '[NHP] nasm not found. sudo apt install nasm' && exit 1)");
        snprintf(cmd,sizeof(cmd),"nasm -f elf64 %s -o /tmp/nhp.o 2>&1",out_file);
        if(system(cmd)==0){snprintf(cmd,sizeof(cmd),"ld /tmp/nhp.o -o /tmp/nhp_out 2>&1");if(system(cmd)==0){system("chmod +x /tmp/nhp_out");printf("--- output ---\n");system("/tmp/nhp_out");printf("--- end ---\n");}}
#endif
    } else {
        printf("\n[NHP] To assemble and run (Linux):\n");
        printf("  nasm -f elf64 %s -o out.o\n",out_file);
        printf("  ld out.o -o out\n");
        printf("  ./out\n\n");
        printf("[NHP] Termux install: pkg install nasm binutils\n\n");
    }

    ob_free(&ob);afree(ast);lx_free(&lx);free(source);
    return 0;
}
