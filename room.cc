#include <iostream>
#include <cstdlib>
#include <sstream>
#include "xchat.h"
#include "TomiTCP/http.h"
#include "TomiTCP/str.h"

using namespace net;

void parse_updateinfo(string s, string &admin, bool &locked)
{
    s.erase(0, s.find(',') + 1);
    string slocked(s, 0, s.find(','));
    locked = atol(slocked.c_str());
    s.erase(0, slocked.length() + 1);
    s.erase(0, s.find(',') + 1);
    s.erase(0, s.find(',') + 1);
    s.erase(s.find(')'));

    if (!s.length()) {
	admin = "";
	return;
    }

    if (s[0] == '\'' || s[0] == '"') {
	s.erase(s.begin());
	s.erase(s.end() - 1);
    }

    strtolower(s);
    admin = s;
}

namespace xchat {
    /*
     * Join room and get all needed info about it
     */
    void XChat::join(const string& rid)
    {
	TomiHTTP s;
	string l;
	room r;

	r.l = -1;
	r.rid = rid;
	r.last_sent = time(0) - idle_interval + 10;

	int ret = s.GET(makeurl2("modchat?op=mainframeset&skin=2&rid="+rid),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while joining channel");
	// we should try to parse if an error occurs
	s.close();
	
	ret = s.GET(makeurl2("modchat?op=textpageng&skin=2&js=1&rid="+rid),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while joining channel");
	while (s.getline(l)) {
	    if (l.find("<select name=\"target\">") != string::npos) {
		while (s.getline(l)) {
		    if (l.find("</select>") != string::npos)
			break;

		    unsigned int pos;
		    l.erase(0, l.find('"') + 1);
		    string nick(l, 0, l.find('"'));
		    l.erase(0, nick.length() + 1);

		    bool muz = 0;
		    if ((pos = l.find(")</option>")) != string::npos && pos != 0) {
			muz = (l[pos-1] == 'M');
		    }

		    if (nick != "~" && nick != "!")
			r.nicklist[strtolower_nr(nick)] = (struct x_nick){nick, muz};
		}
	    }

	    r.nicklist[strtolower_nr(nick)] = (struct x_nick){nick, 2};
	}
	s.close();

	ret = s.GET(makeurl2("modchat?op=roomtopng&skin=2&js=1&rid="+rid),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while joining channel");

	while (s.getline(l)) {
	    static string pat1 = "&inc=1&last_line=";
	    unsigned int a, b;
	    if ((a = l.find(pat1)) != string::npos &&
		    (b = l.find('"', a + pat1.length())) != string::npos) {
		r.l = atol(string(l, a + pat1.length(), b - a - pat1.length()).c_str());
		continue;
	    }

	    static string pat2 = "update_info('";
	    if ((a = l.find(pat2)) != string::npos) {
		parse_updateinfo(string(l, a + pat2.length()), r.admin, r.locked);
	    }
	}

	ret = s.GET(makeurl2("modchat?op=roominfo&skin=2&cid=0&rid="+rid),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while joining channel");

	while (s.getline(l)) {
	    chomp(l);

	    static string pat1 = "název místnosti:</th><td width=260>";
	    unsigned int pos;
	    if ((pos = l.find(pat1)) != string::npos) {
		r.name = recode_to_client(wstrip_nr(string(l, pos + pat1.length())));
		continue;
	    }
	    
	    static string pat2 = "popis místnosti:</th><td>";
	    if ((pos = l.find(pat2)) != string::npos) {
		r.desc = recode_to_client(wstrip_nr(string(l, pos + pat2.length())));
		unsmilize(r.desc);
		continue;
	    }
	    
	    static string pat3 = "stálý správce:</th><td>";
	    if ((pos = l.find(pat3)) != string::npos) {
		stringstream ss(string(l, pos + pat3.length()));
		string admin;
		while (ss >> admin)
		    r.admins.push_back(strtolower_nr(admin));
		continue;
	    }
	}

	if (r.l != -1) {
	    rooms[rid] = r;
	    return;
	}

	throw runtime_error("Parse error");
    }

    /*
     * Leave room.
     */
    void XChat::leave(const string& rid)
    {
	TomiHTTP s;

	int ret = s.GET(makeurl2("modchat?op=mainframeset&skin=2&js=1&menuaction=leave"
		    "&leftroom="+rid),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while parting channel");
	
	rooms.erase(rid);
    }

    /*
     * Get new messages.
     */
    void XChat::getmsg(room& r)
    {
	TomiHTTP s;
	int ret;
	try {
	    ret = s.GET(makeurl2("modchat?op=roomtopng&skin=2&js=1&rid=" + r.rid +
			"&inc=1&last_line=" + inttostr(r.l)),0);
	} catch (...) {
	    // we don't want to die on any request timeout
	    return;
	}
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while getting channels msgs");

	vector<string> dbg;

	r.l = -1;
	string l;
	bool expect_apos = false;
	vector<string> tv;
	while (s.getline(l)) {
	    wstrip(l);
	    if (!l.length()) continue;
	    dbg.push_back(l);

	    // look for next last_line number
	    if (r.l == -1) {
		static string pat1 = "&inc=1&last_line=";
		unsigned int a, b;
		if ((a = l.find(pat1)) != string::npos &&
			(b = l.find('"', a + pat1.length())) != string::npos) {
		    r.l = atol(string(l, a + pat1.length(), b - a - pat1.length()).c_str());
		}
	    }

	    /*
	     * Parse that scary javascript adding new messages to the list.
	     * (yes, this parsing code is much more scary, so try to fix it
	     * only if you are curios, otherwise, just report it's not
	     * working)
	     */
	    if (expect_apos) {
		cout << l << endl;
		expect_apos = false;
		if (l[0] == '\'') {
		    unsigned int pos;
		    if ((pos = l.find('\'', 1)) != string::npos) {
			tv.push_back(string(l, 1, pos - 1));
			if (l[pos + 2] == ',') {
			    expect_apos = true;
			}
		    }
		}
	    } else {
		unsigned int pos1, pos2, pos3;
		static string pat1 = ".addText(", pat2 = "Array('";

		if ((pos1 = l.find(pat1)) != string::npos) {
		    cout << l << endl;
		    if ((pos2 = l.find(pat2, pos1 + pat1.length())) != string::npos) {
			if ((pos3 = l.find('\'', pos2 + pat2.length())) != string::npos) {
			    tv.push_back(string(l, pos2 + pat2.length(),
					pos3 - pos2 - pat2.length()));
			    if (l[pos3 + 1] == ',')
				expect_apos = true;
			}
		    }
		}
	    }

	    /*
	     * Check for current admin and locked status, and eventually emit
	     * appropiate events.
	     */
	    {
		static string pat = "update_info('";
		unsigned int pos;
		if ((pos = l.find(pat)) != string::npos) {
		    string admin;
		    bool locked;
		    parse_updateinfo(string(l,pos+pat.length()), admin, locked);
		    if (r.admin != admin) {
			EvRoomAdminChange *e = new EvRoomAdminChange;
			e->s = "Admin change: " + r.admin + " => " + admin;
			e->rid = r.rid;
			e->before = r.admin;
			e->now = admin;
			r.admin = admin;
			recvq_push(e);
		    }

		    if (r.locked != locked) {
			EvRoomLockChange *e = new EvRoomLockChange;
			e->s = "Locked change: " + r.admin + " => " + admin;
			e->rid = r.rid;
			e->before = r.locked;
			e->now = locked;
			r.locked = locked;
			recvq_push(e);
		    }
		}
	    }

	    /*
	     * If we got a redirect to error page, look at the error message.
	     */
	    // window.open("/~$3249019~2a899f0fea802195d52861d6b8e15c1c/modchat?op=fullscreenmessage&key=403&kicking_nick=&text=", '_top');
	}

	/*
	 * Push messages to recvq in reverse.
	 */
	for (vector<string>::reverse_iterator i = tv.rbegin(); i != tv.rend(); i++)
	    recvq_parse_push(*i, r);

	/*
	 * And don't forget to report parse error if we didn't get valid
	 * last_line.
	 */
	if (r.l == -1) {
	    for (vector<string>::iterator i = dbg.begin(); i != dbg.end(); i++)
		cout << *i << endl;
	    EvRoomError *e = new EvRoomError;
	    e->s = "Parse error";
	    e->rid = r.rid;
	    recvq_push(e);
	}
    }

    /*
     * Send a message.
     */
    void XChat::putmsg(room &r, const string& msg)
    {
	TomiHTTP s;
	int ret = s.POST(makeurl2("modchat"),"op=textpage&skin=2&rid="+r.rid+"&aid=0"+
		"&target=~&textarea="+TomiHTTP::URLencode(msg),0);
	if (ret != 200)
	    throw runtime_error("Not HTTP 200 Ok while posting msg");
	r.last_sent = last_sent = time(0);
    }
}
