#pragma once
#include <string>
#include <vector>
namespace casa {
std::string hashHex(const std::string &data,bool sha256=true);
bool verifySignedPayload(const std::string &envelope,const std::string &publicPem,std::string &payload);
std::string digestAuthorization(const std::string &challenge,const std::string &method,const std::string &uri,const std::string &user,const std::string &password,const std::string &cnonce,unsigned count);
}
