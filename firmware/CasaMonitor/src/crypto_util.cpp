#include "crypto_util.h"
#include <cJSON.h>
#include <cstdio>
#include <map>
#include <cctype>
#ifdef ARDUINO
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/base64.h>
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif
namespace casa {
std::string hashHex(const std::string &s,bool sha){
    unsigned char digest[32]{};size_t size=sha?32:16;
#ifdef ARDUINO
    auto *info=mbedtls_md_info_from_type(sha?MBEDTLS_MD_SHA256:MBEDTLS_MD_MD5);
    if(!info||mbedtls_md(info,reinterpret_cast<const unsigned char *>(s.data()),s.size(),digest))return "";
#else
    unsigned len=0;if(!EVP_Digest(s.data(),s.size(),digest,&len,sha?EVP_sha256():EVP_md5(),nullptr))return "";
#endif
    const char *hex="0123456789abcdef";std::string out;
    for(size_t i=0;i<size;++i){out+=hex[digest[i]>>4];out+=hex[digest[i]&15];}return out;
}
namespace {
std::vector<unsigned char> decode(const std::string &s){
    if(s.empty()||s.size()>12000||s.size()%4)return {};
    for(unsigned char c:s)if(!std::isalnum(c)&&c!='+'&&c!='/'&&c!='=')return {};
    std::vector<unsigned char> out(s.size()*3/4+3);size_t length=0;
#ifdef ARDUINO
    if(mbedtls_base64_decode(out.data(),out.size(),&length,reinterpret_cast<const unsigned char *>(s.data()),s.size()))return {};
#else
    int n=EVP_DecodeBlock(out.data(),reinterpret_cast<const unsigned char *>(s.data()),int(s.size()));if(n<0)return {};
    length=size_t(n);if(s.back()=='=')--length;if(s.size()>1&&s[s.size()-2]=='=')--length;
#endif
    out.resize(length);return out;
}
std::string str(const cJSON *r,const char *k){auto *v=cJSON_GetObjectItemCaseSensitive(r,k);return cJSON_IsString(v)?v->valuestring:"";}
std::string quote(const std::string &s){std::string out;for(unsigned char c:s){if(c<32||c==127)return "";if(c=='"'||c=='\\')out+='\\';out+=char(c);}return out;}
}
bool verifySignedPayload(const std::string &envelope,const std::string &pem,std::string &payload){
    if(envelope.size()>12000)return false;
    auto *r=cJSON_ParseWithOpts(envelope.c_str(),nullptr,true);auto data=decode(str(r,"payload")),signature=decode(str(r,"signature"));cJSON_Delete(r);
    if(data.empty()||data.size()>7000||signature.empty()||signature.size()>80)return false;
    bool ok=false;
#ifdef ARDUINO
    mbedtls_pk_context key;mbedtls_pk_init(&key);unsigned char digest[32];
    auto *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(mbedtls_pk_parse_public_key(&key,reinterpret_cast<const unsigned char *>(pem.c_str()),pem.size()+1)==0&&mbedtls_pk_can_do(&key,MBEDTLS_PK_ECDSA)&&
       mbedtls_md(md,data.data(),data.size(),digest)==0)ok=mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,32,signature.data(),signature.size())==0;
    mbedtls_pk_free(&key);
#else
    BIO *bio=BIO_new_mem_buf(pem.data(),int(pem.size()));EVP_PKEY *key=bio?PEM_read_bio_PUBKEY(bio,nullptr,nullptr,nullptr):nullptr;EVP_MD_CTX *ctx=EVP_MD_CTX_new();
    if(key&&ctx&&EVP_PKEY_base_id(key)==EVP_PKEY_EC&&EVP_DigestVerifyInit(ctx,nullptr,EVP_sha256(),nullptr,key)==1)ok=EVP_DigestVerify(ctx,signature.data(),signature.size(),data.data(),data.size())==1;
    EVP_MD_CTX_free(ctx);EVP_PKEY_free(key);BIO_free(bio);
#endif
    if(ok)payload.assign(reinterpret_cast<const char *>(data.data()),data.size());return ok;
}
std::string digestAuthorization(const std::string &s,const std::string &method,const std::string &uri,const std::string &user,const std::string &password,const std::string &cnonce,unsigned count){
    if(s.rfind("Digest ",0)!=0||s.size()>2048||!count)return "";
    std::map<std::string,std::string> parts;size_t at=7;
    while(at<s.size()){
        while(at<s.size()&&(s[at]==','||s[at]==' '))++at;auto equal=s.find('=',at);if(equal==std::string::npos)break;
        auto key=s.substr(at,equal-at);at=equal+1;std::string value;
        if(at<s.size()&&s[at]=='"'){++at;while(at<s.size()&&s[at]!='"'){if(s[at]=='\\'&&at+1<s.size())++at;value+=s[at++];}if(at>=s.size())return "";++at;}
        else {auto end=s.find(',',at);value=s.substr(at,end==std::string::npos?end:end-at);at=end==std::string::npos?s.size():end;}
        parts[key]=value;
    }
    bool sha=parts["algorithm"]=="SHA-256";
    if((!sha&&parts["algorithm"]!="MD5")||parts["nonce"].empty()||parts["realm"].empty()||parts["qop"]!="auth"||cnonce.empty())return "";
    char nc[9];snprintf(nc,sizeof(nc),"%08x",count);
    auto ha1=hashHex(user+":"+parts["realm"]+":"+password,sha),ha2=hashHex(method+":"+uri,sha);
    auto response=hashHex(ha1+":"+parts["nonce"]+":"+nc+":"+cnonce+":auth:"+ha2,sha);
    for(auto &v:parts)if(v.second.find_first_of("\r\n")!=std::string::npos)return "";
    return "Digest username=\""+quote(user)+"\", realm=\""+quote(parts["realm"])+"\", nonce=\""+quote(parts["nonce"])+"\", uri=\""+quote(uri)+"\", algorithm="+(sha?"SHA-256":"MD5")+", response=\""+response+"\", qop=auth, nc="+nc+", cnonce=\""+quote(cnonce)+"\""+(parts.count("opaque")?", opaque=\""+quote(parts["opaque"])+"\"":"");
}
}
