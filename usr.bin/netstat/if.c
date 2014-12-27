/*-
 * Copyright (c) 2013 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)if.c	8.3 (Berkeley) 4/28/95";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>
#ifdef PF
#include <net/pfvar.h>
#include <net/if_pfsync.h>
#endif

#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <libutil.h>
#ifdef INET6
#include <netdb.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <libxo/xo.h>

#include "netstat.h"

static void sidewaysintpr(int);

#ifdef INET6
static char addr_buf[NI_MAXHOST];		/* for getnameinfo() */
#endif

#ifdef PF
static const char* pfsyncacts[] = {
	/* PFSYNC_ACT_CLR */		"clear all request",
	/* PFSYNC_ACT_INS */		"state insert",
	/* PFSYNC_ACT_INS_ACK */	"state inserted ack",
	/* PFSYNC_ACT_UPD */		"state update",
	/* PFSYNC_ACT_UPD_C */		"compressed state update",
	/* PFSYNC_ACT_UPD_REQ */	"uncompressed state request",
	/* PFSYNC_ACT_DEL */		"state delete",
	/* PFSYNC_ACT_DEL_C */		"compressed state delete",
	/* PFSYNC_ACT_INS_F */		"fragment insert",
	/* PFSYNC_ACT_DEL_F */		"fragment delete",
	/* PFSYNC_ACT_BUS */		"bulk update mark",
	/* PFSYNC_ACT_TDB */		"TDB replay counter update",
	/* PFSYNC_ACT_EOF */		"end of frame mark",
};

static const char* pfsyncacts_name[] = {
	/* PFSYNC_ACT_CLR */		"clear-all-request",
	/* PFSYNC_ACT_INS */		"state-insert",
	/* PFSYNC_ACT_INS_ACK */	"state-inserted-ack",
	/* PFSYNC_ACT_UPD */		"state-update",
	/* PFSYNC_ACT_UPD_C */		"compressed-state-update",
	/* PFSYNC_ACT_UPD_REQ */	"uncompressed-state-request",
	/* PFSYNC_ACT_DEL */		"state-delete",
	/* PFSYNC_ACT_DEL_C */		"compressed-state-delete",
	/* PFSYNC_ACT_INS_F */		"fragment-insert",
	/* PFSYNC_ACT_DEL_F */		"fragment-delete",
	/* PFSYNC_ACT_BUS */		"bulk-update-mark",
	/* PFSYNC_ACT_TDB */		"TDB-replay-counter-update",
	/* PFSYNC_ACT_EOF */		"end-of-frame-mark",
};

#if 0
#define dbg()	do { xo_emit(" **{:d/%d} **\n", __LINE__);fflush(NULL); } while(0)
#undef dbg
#define dbg()	do { } while(0)
#endif

static void
pfsync_acts_stats(const char *list, const char *desc, uint64_t *a)
{
	int i;

	xo_open_list(list);
	for (i = 0; i < PFSYNC_ACT_MAX; i++, a++)
		if (*a || sflag <= 1) {
			xo_open_instance(list);
			xo_emit("\t\t{e:name}{:count/%ju} {N:/%s%s %s}\n",
				pfsyncacts_name[i], (uintmax_t) *a,
				pfsyncacts[i], plural(*a), desc);
			xo_close_instance(list);
		}
	xo_close_list(list);
}

/*
 * Dump pfsync statistics structure.
 */
void
pfsync_stats(u_long off, const char *name, int af1 __unused, int proto __unused)
{
	struct pfsyncstats pfsyncstat, zerostat;
	size_t len = sizeof(struct pfsyncstats);

	if (live) {
		if (zflag)
			memset(&zerostat, 0, len);
		if (sysctlbyname("net.pfsync.stats", &pfsyncstat, &len,
		    zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
			if (errno != ENOENT)
				warn("sysctl: net.pfsync.stats");
			return;
		}
	} else
		kread(off, &pfsyncstat, len);

	xo_emit("{T:/%s}:\n", name);
	xo_open_container(name);

#define	p(f, m) if (pfsyncstat.f || sflag <= 1) \
	xo_emit(m, (uintmax_t)pfsyncstat.f, plural(pfsyncstat.f))

	p(pfsyncs_ipackets, "\t{:received-inet-packets/%ju} "
	  "{N:/packet%s received (IPv4)}\n");
	p(pfsyncs_ipackets6, "\t{:received-inet6-packets/%ju} "
	  "{N:/packet%s received (IPv6)}\n");
	pfsync_acts_stats("input-histogram", "received",
			  &pfsyncstat.pfsyncs_iacts[0]);
	p(pfsyncs_badif, "\t\t/{:dropped-bad-interface/%ju} "
	  "{N:/packet%s discarded for bad interface}\n");
	p(pfsyncs_badttl, "\t\t{:dropped-bad-ttl/%ju} "
	  "{N:/packet%s discarded for bad ttl}\n");
	p(pfsyncs_hdrops, "\t\t{:dropped-short-header/%ju} "
	  "{N:/packet%s shorter than header}\n");
	p(pfsyncs_badver, "\t\t{:dropped-bad-version/%ju} "
	  "{N:/packet%s discarded for bad version}\n");
	p(pfsyncs_badauth, "\t\t{:dropped-bad-auth/%ju} "
	  "{N:/packet%s discarded for bad HMAC}\n");
	p(pfsyncs_badact,"\t\t{:dropped-bad-action/%ju} "
	  "{N:/packet%s discarded for bad action}\n");
	p(pfsyncs_badlen, "\t\t{:dropped-short/%ju} "
	  "{N:/packet%s discarded for short packet}\n");
	p(pfsyncs_badval, "\t\t{:dropped-bad-values/%ju} "
	  "{N:/state%s discarded for bad values}\n");
	p(pfsyncs_stale, "\t\t{:dropped-stale-state/%ju} "
	  "{N:/stale state%s}\n");
	p(pfsyncs_badstate, "\t\t{:dropped-failed-lookup/%ju} "
	  "{N:/failed state lookup\\/insert%s}\n");
	p(pfsyncs_opackets, "\t{:sent-inet-packets/%ju} "
	  "{N:/packet%s sent (IPv4})\n");
	p(pfsyncs_opackets6, "\t{:send-inet6-packets/%ju} "
	  "{N:/packet%s sent (IPv6})\n");
	pfsync_acts_stats("output-histogram", "sent",
			  &pfsyncstat.pfsyncs_oacts[0]);
	p(pfsyncs_onomem, "\t\t{:discarded-no-memory/%ju} "
	  "{N:/failure%s due to mbuf memory error}\n");
	p(pfsyncs_oerrors, "\t\t{:send-errors/%ju} "
	  "{N:/send error%s}\n");
#undef p
	xo_close_container(name);
}
#endif /* PF */

/*
 * Display a formatted value, or a '-' in the same space.
 */
static void
show_stat(const char *fmt, int width, const char *name,
    u_long value, short showvalue)
{
	const char *lsep, *rsep;
	char newfmt[64];

	lsep = "";
	if (strncmp(fmt, "LS", 2) == 0) {
		lsep = " ";
		fmt += 2;
	}
	rsep = " ";
	if (strncmp(fmt, "NRS", 3) == 0) {
		rsep = "";
		fmt += 3;
	}
	if (showvalue == 0) {
		/* Print just dash. */
		xo_emit("{P:/%s}{D:/%*s}{P:/%s}", lsep, width, "-", rsep);
		return;
	}

	/* XXX: workaround {P:} modifier can't be empty and doesn't seem to take args...
	 * so we need to conditionally include it in the format.
	 */
#define maybe_pad(pad)	do {						    \
	if (strlen(pad)) {						    \
		snprintf(newfmt, sizeof(newfmt), "{P:%s}", pad);	    \
		xo_emit(newfmt);					    \
	}								    \
} while (0)

	if (hflag) {
		char buf[5];

		/* Format in human readable form. */
		humanize_number(buf, sizeof(buf), (int64_t)value, "",
		    HN_AUTOSCALE, HN_NOSPACE | HN_DECIMAL);
		snprintf(newfmt, sizeof(newfmt), "%s%%%ds%s", lsep, width, rsep);
		xo_emit(newfmt, buf);
		//fprintf(stderr, "%d -> %s -> %s\n", __LINE__, newfmt, buf);
		xo_attr("value", "%lu", value);
		maybe_pad(lsep);
		snprintf(newfmt, sizeof(newfmt), "{:%s/%%%ds}", name, width);
		xo_emit(newfmt, buf);
		maybe_pad(rsep);
		//fprintf(stderr, "%d -> %s -> %s\n", __LINE__, newfmt, buf);
	} else {
		/* Construct the format string. */
		/*snprintf(newfmt, sizeof(newfmt), "{P:/%zds}{:%s/%%%d%s}{P:/%zds}",
			 strlen(lsep), name, width, fmt, strlen(rsep));*/
		maybe_pad(lsep);
		snprintf(newfmt, sizeof(newfmt), "{:%s/%%%d%s}",
			 name, width, fmt);
		//fprintf(stderr, "%d -> %s -> %lu\n", __LINE__, newfmt, value);
		xo_emit(newfmt, value);
		maybe_pad(rsep);
	}
}

/*
 * Find next multiaddr for a given interface name.
 */
static struct ifmaddrs *
next_ifma(struct ifmaddrs *ifma, const char *name, const sa_family_t family)
{

	for(; ifma != NULL; ifma = ifma->ifma_next) {
		struct sockaddr_dl *sdl;

		sdl = (struct sockaddr_dl *)ifma->ifma_name;
		if (ifma->ifma_addr->sa_family == family &&
		    strcmp(sdl->sdl_data, name) == 0)
			break;
	}

	return (ifma);
}

/*
 * Print a description of the network interfaces.
 */
void
intpr(int interval, void (*pfunc)(char *), int af)
{
	struct ifaddrs *ifap, *ifa;
	struct ifmaddrs *ifmap, *ifma;
	
	if (interval)
		return sidewaysintpr(interval);

	if (getifaddrs(&ifap) != 0)
		err(EX_OSERR, "getifaddrs");
	if (aflag && getifmaddrs(&ifmap) != 0)
		err(EX_OSERR, "getifmaddrs");

	xo_open_list("interface");
	if (!pfunc) {
		if (Wflag)
			xo_emit("{T:/%-7.7s}", "Name");
		else
			xo_emit("{T:/%-5.5s}", "Name");
		xo_emit(" {T:/%5.5s} {T:/%-13.13s} {T:/%-17.17s} "
		       "{T:/%8.8s} {T:/%5.5s} {T:/%5.5s}",
		    "Mtu", "Network", "Address", "Ipkts", "Ierrs", "Idrop");
		if (bflag)
			xo_emit(" {T:/%10.10s}","Ibytes");
		xo_emit(" {T:/%8.8s} {T:/%5.5s}", "Opkts", "Oerrs");
		if (bflag)
			xo_emit(" {T:/%10.10s}","Obytes");
		xo_emit(" {T:/%5s}", "Coll");
		if (dflag)
			xo_emit(" {T:/%s}", "Drop");
		xo_emit("\n");
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		bool network = false, link = false;
		char *name;

		if (interface != NULL && strcmp(ifa->ifa_name, interface) != 0)
			continue;

		name = ifa->ifa_name;

		if (pfunc) {

			(*pfunc)(name);

			/*
			 * Skip all ifaddrs belonging to same interface.
			 */
			while(ifa->ifa_next != NULL &&
			    (strcmp(ifa->ifa_next->ifa_name, name) == 0)) {
				ifa = ifa->ifa_next;
			}
			continue;
		}

		if (af != AF_UNSPEC && ifa->ifa_addr->sa_family != af)
			continue;

		xo_open_instance("interface");

		if (Wflag)
			xo_emit("{tk:name/%-7.7s}", name);
		else
			xo_emit("{tk:name/%-5.5s}", name);

#define IFA_MTU(ifa)	(((struct if_data *)(ifa)->ifa_data)->ifi_mtu)
		show_stat("lu", 6, "mtu", IFA_MTU(ifa), IFA_MTU(ifa));
#undef IFA_MTU

		switch (ifa->ifa_addr->sa_family) {
		case AF_UNSPEC:
			xo_emit("{:network/%-13.13s} ", "none");
			xo_emit("{:address/%-15.15s} ", "none");
			break;
		case AF_INET:
		    {
			struct sockaddr_in *sin, *mask;

			sin = (struct sockaddr_in *)ifa->ifa_addr;
			mask = (struct sockaddr_in *)ifa->ifa_netmask;
			xo_emit("{t:network/%-13.13s} ",
			    netname(sin->sin_addr.s_addr,
			    mask->sin_addr.s_addr));
			xo_emit("{t:address/%-17.17s} ",
			    routename(sin->sin_addr.s_addr));

			network = true;
			break;
		    }
#ifdef INET6
		case AF_INET6:
		    {
			struct sockaddr_in6 *sin6, *mask;

			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			mask = (struct sockaddr_in6 *)ifa->ifa_netmask;

			xo_emit("{t:network/%-13.13s} ",
			    netname6(sin6, &mask->sin6_addr));
			getnameinfo(ifa->ifa_addr, ifa->ifa_addr->sa_len,
			    addr_buf, sizeof(addr_buf), 0, 0, NI_NUMERICHOST);
			xo_emit("{t:address/%-17.17s} ", addr_buf);

			network = 1;
			break;
	            }
#endif /* INET6 */
		case AF_LINK:
		    {
			struct sockaddr_dl *sdl;
			char *cp, linknum[10];
			int n;

			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			cp = (char *)LLADDR(sdl);
			n = sdl->sdl_alen;
			sprintf(linknum, "<Link#%d>", sdl->sdl_index);
			xo_emit("{t:network/%-13.13s} ", linknum);
			{
			    int len = 32;
			    char buf[len];
			    buf[0] = '\0';
			    int z = 0;
			    while ((--n >= 0) && (z < len)) {
				snprintf(buf + z, len - z,
					      "%02x%c", *cp++ & 0xff,
					      n > 0 ? ':' : ' ');
				z += 3;
			    }
			    if (z > 0)
				    xo_emit("{:address/%*s}",
					    32 - z, buf);
			    xo_emit("{]:-32}");
			}
			link = 1;
			break;
		    }
		}

#define	IFA_STAT(s)	(((struct if_data *)ifa->ifa_data)->ifi_ ## s)
		show_stat("lu", 8, "received-packets", IFA_STAT(ipackets), link|network);
		show_stat("lu", 5, "received-errors", IFA_STAT(ierrors), link);
		show_stat("lu", 5, "dropped-packet", IFA_STAT(iqdrops), link);
		if (bflag)
			show_stat("lu", 10, "received-bytes", IFA_STAT(ibytes),
			    link|network);
		show_stat("lu", 8, "sent-packets", IFA_STAT(opackets), link|network);
		show_stat("lu", 5, "send-errors", IFA_STAT(oerrors), link);
		if (bflag)
			show_stat("lu", 10, "sent-bytes", IFA_STAT(obytes), link|network);
		show_stat("NRSlu", 5, "collisions", IFA_STAT(collisions), link);
		if (dflag)
			show_stat("LSlu", 5, "dropped-packets", IFA_STAT(oqdrops), link);
		xo_emit("\n");

		if (!aflag)
			continue;

		/*
		 * Print family's multicast addresses.
		 */
		xo_open_list("multicast-address");
		for (ifma = next_ifma(ifmap, ifa->ifa_name,
		     ifa->ifa_addr->sa_family);
		     ifma != NULL;
		     ifma = next_ifma(ifma, ifa->ifa_name,
		     ifa->ifa_addr->sa_family)) {
			const char *fmt = NULL;

			xo_open_instance("multicast-address");
			switch (ifma->ifma_addr->sa_family) {
			case AF_INET:
			    {
				struct sockaddr_in *sin;

				sin = (struct sockaddr_in *)ifma->ifma_addr;
				fmt = routename(sin->sin_addr.s_addr);
				break;
			    }
#ifdef INET6
			case AF_INET6:

				/* in6_fillscopeid(&msa.in6); */
				getnameinfo(ifma->ifma_addr,
				    ifma->ifma_addr->sa_len, addr_buf,
				    sizeof(addr_buf), 0, 0, NI_NUMERICHOST);
				    /* XXX: "(refs: {:references/%d})\n", */
				xo_emit(
				    "{P:/%*s }{t:address/%-19.19s}",
				    Wflag ? 27 : 25, "", addr_buf);
				break;
#endif /* INET6 */
			case AF_LINK:
			    {
				struct sockaddr_dl *sdl;

				sdl = (struct sockaddr_dl *)ifma->ifma_addr;
				switch (sdl->sdl_type) {
				case IFT_ETHER:
				case IFT_FDDI:
					fmt = ether_ntoa(
					    (struct ether_addr *)LLADDR(sdl));
					break;
				}
				break;
			    }
			}

			if (fmt) {
				xo_emit("{P:/%*s }{t:address/%-17.17s/}",
				    Wflag ? 27 : 25, "", fmt);
				if (ifma->ifma_addr->sa_family == AF_LINK) {
					xo_emit(" {:received-packets/%8lu}",
							IFA_STAT(imcasts));
					xo_emit("{P:/%*s}",
 					    bflag ? 17 : 6, "");
					xo_emit(" {:sent-packets/%8lu}",
							IFA_STAT(omcasts));
 				}
				xo_emit("\n");
			}
			xo_close_instance("multicast-address");
			ifma = ifma->ifma_next;
		}
		xo_close_list("multicast-address");
		xo_close_instance("interface");
	}
	xo_close_list("interface");

	freeifaddrs(ifap);
	if (aflag)
		freeifmaddrs(ifmap);
}

struct iftot {
	u_long	ift_ip;			/* input packets */
	u_long	ift_ie;			/* input errors */
	u_long	ift_id;			/* input drops */
	u_long	ift_op;			/* output packets */
	u_long	ift_oe;			/* output errors */
	u_long	ift_od;			/* output drops */
	u_long	ift_co;			/* collisions */
	u_long	ift_ib;			/* input bytes */
	u_long	ift_ob;			/* output bytes */
};

/*
 * Obtain stats for interface(s).
 */
static void
fill_iftot(struct iftot *st)
{
	struct ifaddrs *ifap, *ifa;
	bool found = false;

	if (getifaddrs(&ifap) != 0)
		xo_err(EX_OSERR, "getifaddrs");

	bzero(st, sizeof(*st));

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		if (interface) {
			if (strcmp(ifa->ifa_name, interface) == 0)
				found = true;
			else
				continue;
		}

		st->ift_ip += IFA_STAT(ipackets);
		st->ift_ie += IFA_STAT(ierrors);
		st->ift_id += IFA_STAT(iqdrops);
		st->ift_ib += IFA_STAT(ibytes);
		st->ift_op += IFA_STAT(opackets);
		st->ift_oe += IFA_STAT(oerrors);
		st->ift_od += IFA_STAT(oqdrops);
		st->ift_ob += IFA_STAT(obytes);
 		st->ift_co += IFA_STAT(collisions);
	}

	if (interface && found == false)
		xo_err(EX_DATAERR, "interface %s not found", interface);

	freeifaddrs(ifap);
}

/*
 * Set a flag to indicate that a signal from the periodic itimer has been
 * caught.
 */
static sig_atomic_t signalled;
static void
catchalarm(int signo __unused)
{
	signalled = true;
}

/*
 * Print a running summary of interface statistics.
 * Repeat display every interval seconds, showing statistics
 * collected over that interval.  Assumes that interval is non-zero.
 * First line printed at top of screen is always cumulative.
 */
static void
sidewaysintpr(int interval)
{
	struct iftot ift[2], *new, *old;
	struct itimerval interval_it;
	int oldmask, line;

	new = &ift[0];
	old = &ift[1];
	fill_iftot(old);

	(void)signal(SIGALRM, catchalarm);
	signalled = false;
	interval_it.it_interval.tv_sec = interval;
	interval_it.it_interval.tv_usec = 0;
	interval_it.it_value = interval_it.it_interval;
	setitimer(ITIMER_REAL, &interval_it, NULL);
	xo_open_list("interface-statistics");

banner:
	xo_emit("{T:/%17s} {T:/%14s} {T:/%16s}\n", "input",
	    interface != NULL ? interface : "(Total)", "output");
	xo_emit("{T:/%10s} {T:/%5s} {T:/%5s} {T:/%10s} {T:/%10s} "
	    "{T:/%5s} {T:/%10s} {T:/%5s}",
	    "packets", "errs", "idrops", "bytes", "packets", "errs", "bytes",
	    "colls");
	if (dflag)
		xo_emit(" {T:/%5.5s}", "drops");
	xo_emit("\n");
	xo_flush();
	line = 0;

loop:
	if ((noutputs != 0) && (--noutputs == 0))
		exit(0);
	oldmask = sigblock(sigmask(SIGALRM));
	while (!signalled)
		sigpause(0);
	signalled = false;
	sigsetmask(oldmask);
	line++;

	fill_iftot(new);

	xo_open_instance("stats");
	show_stat("lu", 10, "received-packets",
	    new->ift_ip - old->ift_ip, 1);
	show_stat("lu", 5, "received-errors",
	    new->ift_ie - old->ift_ie, 1);
	show_stat("lu", 5, "dropped-packets",
	    new->ift_id - old->ift_id, 1);
	show_stat("lu", 10, "received-bytes",
	    new->ift_ib - old->ift_ib, 1);
	show_stat("lu", 10, "sent-packets",
	    new->ift_op - old->ift_op, 1);
	show_stat("lu", 5, "send-errors",
	    new->ift_oe - old->ift_oe, 1);
	show_stat("lu", 10, "sent-bytes",
	    new->ift_ob - old->ift_ob, 1);
	show_stat("NRSlu", 5, "collisions",
	    new->ift_co - old->ift_co, 1);
	if (dflag)
		show_stat("LSlu", 5, "dropped-packets",
		    new->ift_od - old->ift_od, 1);
	xo_close_instance("stats");
	xo_emit("\n");
	xo_flush();

	if (new == &ift[0]) {
		new = &ift[1];
		old = &ift[0];
	} else {
		new = &ift[0];
		old = &ift[1];
	}

	if (line == 21)
		goto banner;
	else
		goto loop;

	/* NOTREACHED */
}
