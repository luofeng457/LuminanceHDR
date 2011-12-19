/**
 * This file is a part of LuminanceHDR package.
 * ---------------------------------------------------------------------- 
 * Copyright (C) 2006,2007,2008 Giuseppe Rota
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ---------------------------------------------------------------------- 
 * 
 * @author Daniel Kaneider <danielkaneider@users.sourceforge.net>
 *  
 */
 
#pragma once

#if defined(__FreeBSD__) || defined(WIN32) || defined(Q_WS_MAC) || defined(__APPLE__)
	#define error(Z) { fprintf(stderr,"%s", Z); exit(1); }
#else
	#include <error.h>
	#define error(Z) error(1,0,"%s", Z);
#endif
