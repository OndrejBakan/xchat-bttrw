#pragma implementation
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "net.h"

namespace net {
    TomiTCP::TomiTCP() : sock(-1), stream(0) {
	memset(&lname,0,sizeof(lname));
	memset(&rname,0,sizeof(rname));
    }

    TomiTCP::TomiTCP(uint16_t port, const string &addr) : sock(-1), stream(0)
    {
	memset(&lname,0,sizeof(lname));
	memset(&rname,0,sizeof(rname));
	listen(port,addr);
    }

    void TomiTCP::listen(uint16_t port, const string &addr)
    {
	if (ok()) {
	    close();
	}

	memset(&lname,0,sizeof(lname));
	lname.sa.sa_family = AF_INET6;
	tomi_pton(addr,lname);
	PORT_SOCKADDR(lname) = htons(port);

	sock = ::socket(lname.sa.sa_family, SOCK_STREAM, 0);
	if (sock < 0 && (errno == EINVAL || errno == EAFNOSUPPORT) && addr == "::") {
                lname.sin.sin_family = AF_INET;
                lname.sin.sin_addr.s_addr = INADDR_ANY;
                
                sock = ::socket(PF_INET, SOCK_STREAM, 0);
        } else if (sock < 0) {
	    throw runtime_error(string(strerror(errno)));
	}

	int set_opt = 1;
        if (::setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&set_opt,sizeof(set_opt)))
	    cerr << "Could not set SO_REUSEADDR" << endl;
	if (::setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&set_opt,sizeof(set_opt)))
	    cerr << "Could not set SO_KEEPALIVE" << endl;

	if (::bind(sock,&lname.sa,SIZEOF_SOCKADDR(lname))) {
	    int er = errno;
	    ::close(sock);
	    throw runtime_error(string(strerror(er)));
	}

	if (::listen(sock,5)) {
	    int er = errno;
	    ::close(sock);
	    throw runtime_error(string(strerror(er)));
	}
    }

    TomiTCP::TomiTCP(const string& hostname, uint16_t port) : sock(-1), stream(0)
    {
	memset(&lname,0,sizeof(lname));
	memset(&rname,0,sizeof(rname));
	connect(hostname,port);
    }

    void TomiTCP::connect(const string& hostname, uint16_t port)
    {
	if (ok()) {
	    close();
	}

	memset(&rname,0,sizeof(rname));

	struct addrinfo *ai,*aip,hints;
	int ret;

	memset(&hints,0,sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(hostname.c_str(),0,&hints,&ai);
	if (ret) {
	    throw runtime_error(string(gai_strerror(ret)));
	}
	aip = ai;

	string err = "No adress to connect to";

	while (!ok()) {
	    if (!aip) {
		freeaddrinfo(ai);
		throw runtime_error(err);
	    }

	    memcpy(&rname,aip->ai_addr,aip->ai_addrlen);
	    rname.sa.sa_family = aip->ai_family;
	    PORT_SOCKADDR(rname) = htons(port);
	    sock = ::socket(aip->ai_family, SOCK_STREAM, 0);
	    if (socket < 0) {
		err = strerror(errno);
	    } else {
		ret = ::connect(sock,(const sockaddr*)&rname,SIZEOF_SOCKADDR(rname));
		if (ret) {
		    close();
		    err = strerror(errno);
		} else {
		    break;
		}
	    }
	    aip = aip->ai_next;
	}

	socklen_t len = SIZEOF_SOCKADDR(lname);
	if (getsockname(sock,(sockaddr*)&lname,&len)) {
	    throw runtime_error(string(strerror(errno)));
	}

	freeaddrinfo(ai);

	stream = fdopen(sock,"r+");
	if (!stream) {
	    close();
	    throw runtime_error(string(strerror(errno)));
	}
	setvbuf(stream,NULL,_IONBF,0); // no buffering
    }

    void resolve(const string& hostname, vector<sockaddr_uni> &addrs) {
	struct addrinfo *ai,*aip,hints;
	int ret;
	
	sockaddr_uni rname;
	memset(&rname,0,sizeof(rname));

	memset(&hints,0,sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(hostname.c_str(),0,&hints,&ai);
	if (ret) {
	    throw runtime_error(string(gai_strerror(ret)));
	}
	aip = ai;

	string err = "No adress for hostname";

	while (aip) {
	    memcpy(&rname,aip->ai_addr,aip->ai_addrlen);
	    rname.sa.sa_family = aip->ai_family;
	    addrs.push_back(rname);

	    aip = aip->ai_next;
	}

	freeaddrinfo(ai);
	if (!addrs.size())
	    throw runtime_error(err);
    }

    string TomiTCP::ident(int ms)
    {
	sockaddr_uni addr = rname;
	PORT_SOCKADDR(addr) = htons(113);
	int s = ::socket(addr.sa.sa_family, SOCK_STREAM, 0);
	if (s < 0) {
	    throw runtime_error(string("Ident: ")+strerror(errno));
	}
	if (::connect(s,(const sockaddr*)&addr,SIZEOF_SOCKADDR(addr))) {
	    int er = errno;
	    ::close(s);
	    throw runtime_error(string("Ident: ")+strerror(er));
	}

	char query[64];
	sprintf(query,"%hi , %hi\n",ntohs(PORT_SOCKADDR(rname)),ntohs(PORT_SOCKADDR(lname)));

	int retval = TEMP_FAILURE_RETRY(::send(s,query,strlen(query),0));
	if (retval < 0) {
	    int er = errno;
	    ::close(s);
	    throw runtime_error(string("Ident: ")+string(strerror(er)));
	}

	char buffer[513];

	if (input_timeout(s,ms) > 0)
	{
	    retval = TEMP_FAILURE_RETRY(::recv(s,buffer,512,0));
	    if (retval <= 0) {
		int er = errno;
		::close(s);
		if (retval < 0)
		    throw runtime_error(string("Ident: ")+string(strerror(er)));
		return "";
	    }
	    buffer[retval] = 0;
	} else {
	    throw timeout("ident Timeout (recv)");
	}
	::close(s);

	stringstream ss(buffer);
	string tmp;

	std::getline(ss,tmp,':');
	ss >> tmp;
	if (tmp != "USERID") {
	    return "";
	} else {
	    std::getline(ss,tmp,':');
	    std::getline(ss,tmp,':');
	    std::getline(ss,tmp,' ');
	    std::getline(ss,tmp);
	    return tmp;
	}
    }

    void TomiTCP::attach(int filedes)
    {
	sock = dup(filedes);
	if (sock < 0)
	    throw runtime_error(string(strerror(errno)));

	stream = fdopen(sock,"r+");
	if (!stream) {
	    close();
	    throw runtime_error(string(strerror(errno)));
	}

	setvbuf(stream,NULL,_IONBF,0); // no buffering
    }

    TomiTCP::~TomiTCP()
    {
	close();
    }

    void TomiTCP::close()
    {
	if (stream) {
	    fclose(stream);
	    stream = 0;
	}

	if (sock >= 0) {
	    ::close(sock);
	    sock = -1;
	}
    }

    bool TomiTCP::ok()
    {
	return (sock >= 0);
    }

    int TomiTCP::nodelay()
    {
	int set_opt = 1;
	int retval = ::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&set_opt,sizeof(set_opt));
	if (retval)
	    cerr << "Could not set TCP_NODELAY" << endl;
	return retval;
    }

    TomiTCP* TomiTCP::accept()
    {
	TomiTCP *ret = new TomiTCP;

	memcpy(&ret->lname,&lname,sizeof(lname));
	memset(&ret->rname,0,sizeof(ret->rname));
	ret->rname.sa.sa_family = lname.sa.sa_family;

	socklen_t len = SIZEOF_SOCKADDR(ret->rname);
	ret->sock = TEMP_FAILURE_RETRY(::accept(sock,&ret->rname.sa,&len));
	if (ret->sock < 0)
	    throw runtime_error(string(strerror(errno)));

	ret->stream = fdopen(ret->sock,"r+");
	if (!ret->stream) {
	    delete ret;
	    throw runtime_error(string(strerror(errno)));
	}
	setvbuf(ret->stream,NULL,_IONBF,0); // no buffering
	
	return ret;
    }

    void TomiTCP::send(const char* buf, int sz, unsigned int ms)
    {
	while (sz) {
	    if (output_timeout(sock,ms) <= 0) {
		throw timeout("Timeout (send)");
	    }
	    int retval = TEMP_FAILURE_RETRY(::send(sock,buf,sz,0));
	    if (retval < 0) {
		throw runtime_error(string(strerror(errno)));
	    }
	    buf += retval;
	    sz -= retval;
	}
    }

    int TomiTCP::recv(char* buf, int sz, unsigned int ms)
    {
	if (input_timeout(sock,ms) > 0)
        {
            int retval = TEMP_FAILURE_RETRY(::recv(sock,buf,sz,0));
            if (retval <= 0) {
		if (retval < 0)
		    throw runtime_error(string(strerror(errno)));
            }
	    return retval;
        }
	throw timeout("Timeout (recv)");
    }

    /*
     * Only for compatibility with older programs, will be removed on next
     * cleanup.
     */
    FILE* TomiTCP::makestream()
    {
	int s = dup(sock);
	if (s < 0)
	    throw runtime_error(string(strerror(errno)));

	FILE *f = fdopen(s,"r+");
	if (!f)
	    throw runtime_error(string(strerror(errno)));
	setvbuf(f,NULL,_IONBF,0); // no buffering

	return f;
    }

    TomiTCP::operator FILE* ()
    {
	return stream;
    }

    int TomiTCP::getline(string& s)
    {
	char *buf;
	size_t len = 0;

	int ret = ::getline(&buf,&len,stream);
	if (ret == -1)
	    return 0;
	s = buf;
	free(buf);
	return 1;
    }

    string tomi_ntop(const sockaddr_uni& name)
    {
	char tmp[128],*p;

	if (!inet_ntop(name.sa.sa_family,(name.sa.sa_family == AF_INET)?
		    ((const void *)&name.sin.sin_addr):
		    ((const void *)&name.sin6.sin6_addr),tmp,128))
	    throw runtime_error(string(strerror(errno)));

	if (name.sa.sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&name.sin6.sin6_addr))
	    if ((p = strstr(tmp,":ffff:")) || (p = strstr(tmp, ":FFFF:")))
		return string(p+6);

	return string(tmp);
    }
    
    void tomi_pton(string p, sockaddr_uni& name)
    {
	int ret;

	if (name.sa.sa_family == AF_INET6 && p.length() && ((int)p.find_first_of(':'))==-1)
	    name.sa.sa_family = AF_INET;
	    //p = "::ffff:" + p;
	ret = inet_pton(name.sa.sa_family,p.c_str(),(name.sa.sa_family == AF_INET)?
		((void *)&name.sin.sin_addr):
		((void *)&name.sin6.sin6_addr));
	if (ret < 0)
	    throw runtime_error(string(strerror(errno)));
	if (ret == 0)
	    throw runtime_error("Not a valid address");
    }
    
    string revers(const sockaddr_uni& name)
    {
	char hostname[NI_MAXHOST];
	sockaddr_uni n;
	memset(&n,0,sizeof(name));

	if (name.sa.sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&name.sin6.sin6_addr)) {
	    n.sa.sa_family = AF_INET;
	    tomi_pton(tomi_ntop(name),n);
	} else {
	    n = name;
	}

	if (getnameinfo((const sockaddr*)&n,SIZEOF_SOCKADDR(name),hostname,sizeof(hostname),0,0,NI_NAMEREQD))
	    return tomi_ntop(name);
	return string(hostname);
    }

    int input_timeout(int filedes, unsigned int ms)
    {
	fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(filedes, &set);

        timeout.tv_sec = ms / 1000;
        timeout.tv_usec = (ms % 1000) * 1000;
        return TEMP_FAILURE_RETRY(select(FD_SETSIZE,&set,NULL,NULL,&timeout));
    }

    int output_timeout(int filedes, unsigned int ms)
    {
	fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(filedes, &set);

        timeout.tv_sec = ms / 1000;
        timeout.tv_usec = (ms % 1000) * 1000;
        return TEMP_FAILURE_RETRY(select(FD_SETSIZE,NULL,&set,NULL,&timeout));
    }
}
