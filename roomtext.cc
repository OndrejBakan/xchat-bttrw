#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <iostream>
#include "xchat.h"
#include "smiles.h"
#include "idle.h"
#include "TomiTCP/str.h"

namespace xchat {
    /**
     * Recode from ISO-8859-2 to #client_charset.
     * \param s Input string.
     * \return Recoded string if ok, original string if an error has occured.
     */
    string XChat::recode_to_client(string s) {
	if (!client_charset.length())
	    return s;

	try {
	    /*
	     * Some exceptions...
	     */
	    if (client_charset != "ISO-8859-2") {
		string::size_type a, pos = 0;

		while ((a = s.find("\xe2\x97\x8f", pos)) != string::npos) {
		    s.replace(a, 3, "*");
		    pos = a + 1;
		}
	    }

	    return recode(s, "ISO-8859-2", client_charset);
	} catch (runtime_error &er) {
	    auto_ptr<EvError> e(new EvError);
	    e->s = er.what();
	    recvq_push((auto_ptr<Event>) e);
	    return s;
	}
    }

    /**
     * Recode from #client_charset to ISO-8859-2.
     * \param s Input string.
     * \return Recoded string if ok, original string if an error has occured.
     */
    string XChat::recode_from_client(string s) {
	if (!client_charset.length())
	    return s;

	try {
	    return recode(s, client_charset, "ISO-8859-2");
	} catch (runtime_error &er) {
	    auto_ptr<EvError> e(new EvError);
	    e->s = er.what();
	    recvq_push((auto_ptr<Event>) e);
	    return s;
	}
    }

    /**
     * Strip JavaScript escapes. This is done in-place.
     * \param s Input/output string.
     */
    void XChat::stripjsescapes(string &s)
    {
	string::size_type a, pos = 0;

	while ((a = s.find("\\", pos)) != string::npos) {
	    if (s.begin() + a + 2 <= s.end()) {
		s.erase(s.begin() + a);
		pos = a + 1;
	    } else
		break;
	}
    }

    /**
     * Strip HTML tags. Convert graphical smiles to text format, convert br
     * tags to "|" to separate lines. This is done in-place.
     * \param s Input/output string.
     */
    void XChat::striphtml(string &s)
    {
	string::size_type a, b, lastbr = string::npos, pos = 0;

	while (((a = s.find('<', pos)) != string::npos) &&
		((b = string(s, a).find('>')) != string::npos)) {
	    int smile = 0;
	    bool br = 0;
	    string link = "";

	    {
		static string pat = "<img src=\"https://x.ximg",
		    pat2 = ".gif\"", pat3 = "/sm/";
		string::size_type c, d;
		if (!s.compare(a, pat.length(), pat) &&
			(c = s.find(pat2, a)) != string::npos &&
			(d = s.find(pat3, a)) != string::npos &&
			c < a + b && d < c) {
		    while (--c > a + pat.length() && isdigit(s[c])); c++;
		    smile = atoi(string(s, c).c_str());
		}
	    }

	    {
		static string pat = "<a href=\"https://redir.xchat.cz/~guest~/?url=",
		    pat2 = "</a>", pat3 = "\" target";
		string::size_type c, d;
		if (!s.compare(a, pat.length(), pat) &&
			(c = s.find(pat2, a)) != string::npos &&
			(d = s.find(pat3, a)) != string::npos &&
			c > a + b && d < c) {
		    link = net::TomiHTTP::URLdecode(string(s, a + pat.length(),
				d - a - pat.length()).c_str());
		    b = c + pat2.length() - 1 - a;
		}
	    }

	    {
		static string pat = "<br";
		if (!s.compare(a, pat.length(), pat))
		    br = 1;
	    }

	    s.erase(s.begin() + a, s.begin() + a + b + 1);
	    pos = a;

	    if (smile) {
		s.insert(a, "*" + tostr(smile) + "*");
		pos += ("*" + tostr(smile) + "*").length();
	    }
	    if (!link.empty()) {
		s.insert(a, link);
		pos += link.length();
	    }
	    if (br) {
		s.insert(a, " | ");
		lastbr = a;
		pos += 3;
	    }
	}

	/*
	 * Erase last br with a trailing whitespace, if there's no text.
	 */
	if (lastbr != string::npos &&
		s.find_first_not_of(" \f\n\r\t\v|", lastbr) == string::npos)
	    s.erase(lastbr);
    }

    /**
     * Strip and get date from the message, if it is there.
     * \param m Input/output string.
     * \param date Output string for date.
     */
    void XChat::getdate(string &m, string &date)
    {
	string n = m, d;

	n.erase(0, n.find_first_not_of(" "));
	d = string(n, 0, n.find_first_of(" "));
	n.erase(0, n.find_first_of(" "));
	n.erase(0, n.find_first_not_of(" "));

	date = d;

	d.erase(0, d.find_first_of(":"));
	d.erase(d.find_last_of(":") + 1);
	if (d.length() > 1 && d[0] == ':' && d[d.length()-1] == ':')
	    m = n;
	else
	    date = "";
    }

    /**
     * Get nick (source) and, if given, target nick. Strip [room], if
     * necessary.
     * \param m Input/output string.
     * \param src Output string for nick/source.
     * \param target Output string for target.
     */
    void XChat::getnick(string &m, string &src, string &target)
    {
	string t = string(m, 0, m.find(": "));
	if (t.length() == m.length())
	    return;
	m.erase(0, t.length() + 2);
	if (m.length() && m[0] == ' ')
	    m.erase(m.begin());

	t.erase(0, t.find_first_not_of(" "));
	src = string(t, 0, t.find("->"));
	if (src.length() != t.length())
	    target = string(t, src.length() + 2);

	// strip [room]
	string::size_type a;
	if (src[0] == '[' && ((a = src.find(']')) != string::npos))
	    src.erase(0, a + 1);

	if (target[0] == '[' && ((a = target.find(']')) != string::npos))
	    target.erase(0, a + 1);

	if (src.find(' ') != string::npos || target.find(' ') != string::npos) {
	    src = "";
	    target = "";
	}
    }

    /**
     * Convert xchat smilies to human readable ones using the #smiles table.
     * This is done in-place.
     * \param s Input/output string.
     */
    void XChat::unsmilize(string &s)
    {
	string::size_type a, b, pos = 0;

	if (!convert_smiles)
	    return;

	while (((a = s.find('*', pos)) != string::npos) &&
		((b = s.find('*', a + 1)) != string::npos)) {
	    bool fail = 0;
	    for (string::iterator i = s.begin() + a + 1; i != s.begin() + b; i++)
		fail |= !isdigit(*i);

	    if (!fail) {
		string smile_s = string(s.begin() + a + 1, s.begin() + b);
		int smile = atoi(smile_s.c_str());
		if (smile < smiles_count && smile >= 0 && smiles[smile]) {
		    int add;

		    if (convert_smiles == 2) {
			string repl = string("\002") + smiles[smile] + "(" +
			    smile_s + ")\002";
	    		s.replace(a, b - a + 1, repl);
			add = repl.length();
		    } else {
			string repl = string("\002") + smiles[smile] + "\002";
	    		s.replace(a, b - a + 1, repl);
			add = repl.length();
		    }

		    if (s[a + add] != ' ') {
			s.insert(a + add, " ");
			add++;
		    }
		    if (a > 0 && s[a - 1] != ' ') {
			s.insert(a, " ");
			add++;
		    }

		    pos = a + add;
		} else {
		    pos = a + 1;
		}
	    } else {
		pos = a + 1;
	    }
	}
    }

    /**
     * Check for user joining a room. Store its nick and sex. Add it to the
     * nicklist.
     * \param r The room the message belongs to.
     * \param m The message.
     * \param src Output string for nick.
     * \param sex Output string for sex.
     * \return True if it was a join.
     */
    bool XChat::isjoin(room& r, string &m, string &src, int &sex)
    {
	string::size_type a,b;
	if ((a = m.find("U�ivatel")) != string::npos &&
		(((b = m.find("vstoupil  do m�stnosti")) != string::npos) ||
		 ((b = m.find("vstoupila do m�stnosti")) != string::npos))) {
	    if (m.find("U�ivatelka") != string::npos) {
		src = string(m, a + sizeof("U�ivatelka ") - 1, b - a - sizeof("U�ivatelka ") + 1);
		wstrip(src);
		r.nicklist[strtolower_nr(src)] = (struct x_nick){src, 0};
		sex = 0;
	    } else {
		src = string(m, a + sizeof("U�ivatel ") - 1, b - a - sizeof("U�ivatel ") + 1);
		wstrip(src);
		r.nicklist[strtolower_nr(src)] = (struct x_nick){src, 1};
		sex = 1;
	    }
	    return 1;
	}

	return 0;
    }

    /**
     * Check for user leaving a room. Store its nick and sex. Delete if from
     * the nicklist.
     * \param r The room the message belongs to.
     * \param m The message.
     * \param src Output string for nick.
     * \param sex Output string for sex.
     * \return True if it was a leave.
     */
    bool XChat::isleave(room& r, string &m, string &src, int &sex)
    {
	string::size_type a,b;
	if ((a = m.find("U�ivatel")) != string::npos &&
		(((b = m.find("opustil  m�stnost")) != string::npos) ||
		 ((b = m.find("opustila m�stnost")) != string::npos))) {
	    if (m.find("U�ivatelka") != string::npos) {
		src = string(m, a + sizeof("U�ivatelka ") - 1, b - a - sizeof("U�ivatelka ") + 1);
		wstrip(src);
	    } else {
		src = string(m, a + sizeof("U�ivatel ") - 1, b - a - sizeof("U�ivatel ") + 1);
		wstrip(src);
	    }

	    if (r.nicklist.find(strtolower_nr(src)) == r.nicklist.end())
		sex = 2;
	    else
		sex = r.nicklist[strtolower_nr(src)].sex;
	    r.nicklist.erase(strtolower_nr(src));
	    return 1;
	}

	return 0;
    }

    /**
     * Check for user being kicked from a room. Store its nick, kicker's nick
     * and kickee's sex. Delete it from the nicklist.
     * \param r The room the message belongs to.
     * \param m The message.
     * \param src Output string for nick.
     * \param reason Output string for kick reason.
     * \param who Output string for kicker's nick.
     * \param sex Output string for kickee's sex.
     * \return True if it was a kick.
     */
    bool XChat::iskick(room& r, string &m, string &src, string &reason, string &who, int &sex)
    {
	string::size_type a,b;
	if ((a = m.find("U�ivatel")) != string::npos &&
		(((b = m.find("byl  vyhozen")) != string::npos) ||
		(b = m.find("byla vyhozena")) != string::npos)) {
	    if (m.find("U�ivatelka") != string::npos) {
		src = string(m, a + sizeof("U�ivatelka ") - 1, b - a - sizeof("U�ivatelka ") + 1);
		wstrip(src);
	    } else {
		src = string(m, a + sizeof("U�ivatel ") - 1, b - a - sizeof("U�ivatel ") + 1);
		wstrip(src);
	    }

	    if ((a = m.find("spr�vcem")) != string::npos &&
		    (b = m.find("z m�stnosti")) != string::npos) {
		who = string(m, a + sizeof("spr�vcem ") - 1, b - a - sizeof("spr�vcem ") + 1);
		wstrip(who);
	    }

	    if ((a = m.find("administr�torem")) != string::npos &&
		    (b = m.find("z m�stnosti")) != string::npos) {
		who = string(m, a + sizeof("administr�torem ") - 1,
			b - a - sizeof("administr�torem ") + 1);
		wstrip(who);
	    }

	    if ((a = m.find_last_of("(")) != string::npos &&
		    (b = m.find_last_of(")")) != string::npos) {
		reason = string(m, a + 1, b - a - 1);
	    }

	    if (r.nicklist.find(strtolower_nr(src)) == r.nicklist.end())
		sex = 2;
	    else
    		sex = r.nicklist[strtolower_nr(src)].sex;
	    r.nicklist.erase(strtolower_nr(src));
	    return 1;
	}

	return 0;
    }

    /**
     * Check for user being killed from all rooms. Store its nick, killer's nick
     * and killee's sex. Delete it from the nicklist.
     * \param r The room the message belongs to.
     * \param m The message.
     * \param src Output string for nick.
     * \param who Output string for killer's nick.
     * \param sex Output string for killee's sex.
     * \return True if it was a kill.
     */
    bool XChat::iskill(room& r, string &m, string &src, string &who, int &sex)
    {
	string::size_type a,b;
	if ((a = m.find("U�ivatel")) != string::npos &&
		(((b = m.find("byl  vyhozen")) != string::npos) ||
		(b = m.find("byla vyhozena")) != string::npos)) {
	    if (m.find("U�ivatelka") != string::npos) {
		src = string(m, a + sizeof("U�ivatelka ") - 1, b - a - sizeof("U�ivatelka ") + 1);
		wstrip(src);
	    } else {
		src = string(m, a + sizeof("U�ivatel ") - 1, b - a - sizeof("U�ivatel ") + 1);
		wstrip(src);
	    }

	    if ((a = m.find("administr�torem")) != string::npos &&
		    (b = m.find("ze v�ech m�stnost�")) != string::npos) {
		who = string(m, a + sizeof("administr�torem ") - 1,
			b - a - sizeof("administr�torem ") + 1);
		wstrip(who);
	    } else
		return 0;

	    if (r.nicklist.find(strtolower_nr(src)) == r.nicklist.end())
		sex = 2;
	    else
    		sex = r.nicklist[strtolower_nr(src)].sex;
	    r.nicklist.erase(strtolower_nr(src));
	    return 1;
	}

	return 0;
    }

    /**
     * Check for advertisement messages. Extract the link. This has to be
     * called prior to all stripping.
     * \param m The message.
     * \param link Output string for link.
     * \return True if it was an advert.
     */
    bool XChat::isadvert(string &m, string &link)
    {
	static string pat = "<A TARGET=_blank HREF=\\\"/advert/advert.php";
	string::size_type pos, pos2;
	if ((pos = m.find(pat)) != string::npos) {
	    link = "https://www.xchat.cz/advert/advert.php" +
		string(m, pos + pat.length());
	    link.erase(link.find("\\\""));
	    return 1;
	}

	static string pat2 = "<a href=\\\"https://", pat3 = " target=\\\"_new\\\">";
	if ((pos = m.find(pat2)) != string::npos &&
		(pos2 = m.find("\\\"", pos + pat2.length())) != string::npos) {
	    link = "https://" +
		string(m, pos + pat2.length(), pos2 - pos - pat2.length());
	    if (pos2 + 2 < m.length() && !m.compare(pos2 + 2, pat3.length(), pat3))
		return 1;
	}

	return 0;
    }

    /**
     * Check, if the given system message has a global meaning and should
     * therefore go as EvSysMsg instead of EvRoomSysMsg.
     * \param m The message.
     * \param stopdup Should we stop this message from duplicating?
     * \return True if it was an advert.
     */
    bool XChat::sysnoroom(string &m, bool &stopdup)
    {
	static string pat1 = "Info: ";
	if (!m.compare(0, pat1.length(), pat1)) {
	    return true;
	}

	static string pat2 = "INFO: ";
	if (!m.compare(0, pat2.length(), pat2)) {
	    return true;
	}

	static string pat3 = "Info2: ";
	if (!m.compare(0, pat3.length(), pat3)) {
	    return true;
	}

	static string pat4 = "nen� v ��dn� m�stnosti";
	if (m.find(pat4) != string::npos) {
	    return true;
	}

	static string pat5 = "neexistuje";
	if (m.find(pat5) != string::npos) {
	    return true;
	}

	static string pat6 = "Zaps�no pro";
	if (m.find(pat6) != string::npos) {
	    return true;
	}

	static string pat7 = "vstoupil do m�stnosti";
	if (watch_global && m.find(pat7) != string::npos) {
	    stopdup = true;
	    return true;
	}

        static string pat8 = "vstoupila do m�stnosti";
        if (watch_global && m.find(pat8) != string::npos) {
	    stopdup = true;
	    return true;
	}

	return false;
    }

    /**
     * Parse a line from xchat and push appropiate Event to the #recvq.
     * \param m The line.
     * \param r The room the line belongs to.
     */
    void XChat::recvq_parse_push(string m, room& r)
    {
	string link;
	bool advert = isadvert(m, link);

	x_nick *n;

	stripjsescapes(m);
	striphtml(m);
	striphtmlent(m);

	string date;
	getdate(m, date);

	if (advert) {
	    auto_ptr<EvRoomAdvert> e(new EvRoomAdvert);
	    e->s = recode_to_client(m);
	    e->rid = r.rid;
	    e->link = link;
	    e->d = date;
	    recvq_push((auto_ptr<Event>) e);
	    return;
	}

	string src, target;
	getnick(m, src, target);

	if (src.length()) {
	    unsmilize(m);

	    if (strtolower_nr(src) == "system" &&
		    strtolower_nr(target) == strtolower_nr(me.nick)) {
		bool stopdup = false;
		if (checkidle(wstrip_nr(m))) {
		    auto_ptr<EvRoomIdlerMsg> e(new EvRoomIdlerMsg);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		} else if (sysnoroom(m, stopdup)) {
		    auto_ptr<EvSysMsg> e(new EvSysMsg);
		    e->s = recode_to_client(m);
		    e->d = date;
		    e->stopdup = stopdup;
		    recvq_push((auto_ptr<Event>) e);
		} else {
		    auto_ptr<EvRoomSysMsg> e(new EvRoomSysMsg);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		}
	    } else if (target.length() && strtolower_nr(src) != strtolower_nr(me.nick)) {
		auto_ptr<EvWhisper> e(new EvWhisper);
		e->s = recode_to_client(m);
		e->src = (struct x_nick){ src, (n = findnick(src, 0))?n->sex:2 };
		e->target = (struct x_nick){ target, (n = findnick(target, 0))?n->sex:2 };
		e->d = date;
		recvq_push((auto_ptr<Event>) e);
	    } else if (strtolower_nr(src) != strtolower_nr(me.nick)) {
		auto_ptr<EvRoomMsg> e(new EvRoomMsg);
		e->s = recode_to_client(m);
		e->rid = r.rid;
		e->src = (struct x_nick){ src, (n = findnick(src, 0))?n->sex:2 };
		e->d = date;
		recvq_push((auto_ptr<Event>) e);
	    }
	} else {
	    int sex;
	    string reason, who;

	    if (isjoin(r, m, src, sex)) {
		if (strtolower_nr(src) != strtolower_nr(me.nick)) {
		    auto_ptr<EvRoomJoin> e(new EvRoomJoin);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->src = (struct x_nick){ src, sex };
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		}
	    } else if (isleave(r, m, src, sex)) {
		auto_ptr<EvRoomLeave> e(new EvRoomLeave);
		e->s = recode_to_client(m);
		e->rid = r.rid;
		e->src = (struct x_nick){ src, sex };
		e->d = date;
		recvq_push((auto_ptr<Event>) e);
	    } else if (iskill(r, m, src, who, sex)) {
		auto_ptr<EvKill> e(new EvKill);
		e->s = recode_to_client(m);
		e->src = (struct x_nick){ who, (n = findnick(who, 0))?n->sex:2 };
		e->target = (struct x_nick){ src, sex };
		e->d = date;
		e->stopdup = true;
		recvq_push((auto_ptr<Event>) e);
	    } else if (iskick(r, m, src, reason, who, sex)) {
		if (who.length()) {
		    auto_ptr<EvRoomKick> e(new EvRoomKick);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->src = (struct x_nick){ who, (n = findnick(who, 0))?n->sex:2 };
		    e->target = (struct x_nick){ src, sex };
		    e->reason = recode_to_client(reason);
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		} else {
		    auto_ptr<EvRoomLeave> e(new EvRoomLeave);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->src = (struct x_nick){ src, sex };
		    e->reason = recode_to_client(reason);
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		}
	    } else {
		auto_ptr<EvRoomSysText> e(new EvRoomSysText);
		e->s = recode_to_client(m);
		e->rid = r.rid;
		e->d = date;
		recvq_push((auto_ptr<Event>) e);
	    }
	}
    }

    /**
     * Parse a line from xchat backlog and push appropiate Event to the
     * #recvq.
     * \param m The line.
     * \param r The room the line belongs to.
     */
    void XChat::recvq_parse_push_history(string m, room& r)
    {
	string link;
	bool advert = isadvert(m, link);

	stripjsescapes(m);
	striphtml(m);
	striphtmlent(m);

	string date;
	getdate(m, date);

	if (advert) {
	    auto_ptr<EvRoomAdvert> e(new EvRoomAdvert);
	    e->s = recode_to_client(m);
	    e->rid = r.rid;
	    e->link = link;
	    e->d = date;
	    recvq_push((auto_ptr<Event>) e);
	    return;
	}

	string src, target;
	getnick(m, src, target);

	if (src.length()) {
	    unsmilize(m);

	    if (strtolower_nr(src) == "system" &&
		    strtolower_nr(target) == strtolower_nr(me.nick)) {
		if (checkidle(wstrip_nr(m))) {
		    auto_ptr<EvRoomIdlerMsg> e(new EvRoomIdlerMsg);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		} else {
		    auto_ptr<EvRoomSysMsg> e(new EvRoomSysMsg);
		    e->s = recode_to_client(m);
		    e->rid = r.rid;
		    e->d = date;
		    recvq_push((auto_ptr<Event>) e);
		}
		/*
		 * No sysnoroom support, what would it be useful for? -lis
		 */
	    } else {
		auto_ptr<EvRoomHistoryMsg> e(new EvRoomHistoryMsg);
		e->src = src;
		e->target = target;
		e->s = recode_to_client(m);
		e->rid = r.rid;
		e->d = date;
		recvq_push((auto_ptr<Event>) e);
	    }
	} else {
	    auto_ptr<EvRoomSysText> e(new EvRoomSysText);
	    e->s = recode_to_client(m);
	    e->rid = r.rid;
	    e->d = date;
	    recvq_push((auto_ptr<Event>) e);
	}
    }
}
