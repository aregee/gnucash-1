#! /usr/bin/perl -w
#
#    Copyright (C) 1998, Dj Padzensky <djpadz@padz.net>
#    Copyright (C) 1998, 1999 Linas Vepstas <linas@linas.org>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

# This code derived from Padzensky's work on package Finance::YahooQuote,
# but extends its capabilites to encompas a greater number of data sources.
#
# package Finance::YahooQuote;
package Quote;
require 5.000;

require Exporter;
use strict;
use vars qw($VERSION @EXPORT @ISA $QURL);

use LWP::UserAgent;
use HTTP::Request::Common;

$VERSION = '0.06';
$QURL = ("http://quote.yahoo.com/d?f=snl1d1t1c1p2va2bapomwerr1dyj1&s=");
@ISA = qw(Exporter);

# Don't export; let user invoke with Quote::getquote syntax.
# @EXPORT = qw(&quote_yahoo);

sub quote_yahoo {
    my @symbols = @_;
    my($x,@q,@qr,@qa,%aa,$ua,$url,$sym);
    $x = $";
    $" = "+";
    $url = $QURL."@symbols";
    $" = $x;
    $ua = LWP::UserAgent->new;
    foreach (split('\n',$ua->request(GET $url)->content)) {
	@q = grep { s/^"?(.*?)\s*"?\s*$/$1/; } split(',');
        $sym = $q[0];
        $aa {$sym, "exchange"} = "NYSE";  # new  york stock exchange
        $aa {$sym, "name"} = $q[1];
        $aa {$sym, "last"} = $q[2];
        $aa {$sym, "lastdate"} = $q[3];
        $aa {$sym, "lasttime"} = $q[4];
        $aa {$sym, "volume"} = $q[7];
        $aa {$sym, "bid"} = $q[9];
        $aa {$sym, "ask"} = $q[10];
        $aa {$sym, "close"} = $q[11];
        $aa {$sym, "open"} = $q[12];
        $aa {$sym, "eps"} = $q[15];
        $aa {$sym, "pe"} = $q[16];
        $aa {$sym, "cap"} = $q[20];
    }
    # return wantarray() ? @qr : \@qr;
    return %aa;
}

__END__

1;

=head1 NAME

Finance::Quote - Get stock and mutual fund quotes from various exchanges

=head1 SYNOPSIS

  use Finance::Quote;
  %quotes = Quote::quote_yahoo @symbols;	# Get quotes for a bunch of symbols
  %quotes = Quote::quote_fidelity @symbols;	# Get quotes for a bunch of symbols
  print ("the last price was ", $quotes {"IBM", "last"} );

=head1 DESCRIPTION

This module gets stock quotes from various internet sources, including 
Yahoo! Finance and Fidelity Investments.  The
B<quote_yahoo> function will return a quote for each of the stock symbols
passed to it.  The return value of each of the routines is an associative
array, which may include one or more of the following elements:

    name         Company Name
    last         Last Price
    date         Last Trade Date
    time         Last Trade Time
                 Change
                 Percent Change
    volume       Volume
                 Average Daily Vol
    bid          Bid
    ask          Ask
    close        Previous Close
    open         Today's Open
                 Day's Range
                 52-Week Range
    eps          Earnings per Share
    pe           P/E Ratio
                 Dividend Pay Date
                 Dividend per Share
                 Dividend Yield
    cap          Market Capitalization

=head1 COPYRIGHT

Copyright 1998, Dj Padzensky
Copyright 1998, 1999 Linas Vepstas

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

The information that you obtain with this library may be copyrighted
by Yahoo! Inc., and is governed by their usage license.  See
http://www.yahoo.com/docs/info/gen_disclaimer.html for more
information.

=head1 AUTHOR

Dj Padzensky (C<djpadz@padz.net>), PadzNet, Inc.
Linas Vepstas (C<linas@linas.org>)

The Finance::YahooQuote home page can be found at
http://www.padz.net/~djpadz/YahooQuote/
The GnuCash home page can be found at
http://www.gnucash.org/

=cut
