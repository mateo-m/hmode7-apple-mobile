#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>

#define DIVISE(a, b) ((b)!=0?((a)/(b)):(0))
#define RS(target, source, shift) {\
 target = source;\
 if (target < 0) {\
  target = -(1 - target >> shift);\
 } else {\
  target = target >> shift;\
 }\
}
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)

/***********************
* STRUCT BITMAP (poccil)
************************/

typedef struct{
 DWORD unk1;
 DWORD unk2;
 BITMAPINFOHEADER *infoheader;
 RGBQUAD *firstRow;
 RGBQUAD *lastRow;
} RGSSBMINFO;

typedef struct{
 DWORD unk1;
 DWORD unk2;
 RGSSBMINFO *bminfo;
} BITMAPSTRUCT;

typedef struct{
 DWORD flags;
 DWORD klass;
 void (*dmark)(void*);
 void (*dfree)(void*);
 BITMAPSTRUCT *bm;
} RGSSBITMAP;

/****************
* STRUCT ARRAY
*****************/

typedef unsigned long VALUE;

struct RBasic {
    unsigned long flags;
    VALUE klass;
};

struct RArray {
    struct RBasic basic;
    long len;
    union {
	long capa;
	VALUE shared;
    } aux;
    VALUE *ptr;
};

typedef struct RArray RArray;

/****************
* STRUCT TABLE
*****************/

typedef signed short SWORD;

struct RTableData {
    struct RBasic basic;
	DWORD nbDim;
	DWORD xsize;
	DWORD ysize;
	DWORD zsize;
	DWORD size;
    SWORD *data;
};

typedef struct RTableData RTableData;

struct RTable {
    struct RBasic basic;
	DWORD unknown1;
	DWORD unknown2;
    RTableData *rTableData;
};

typedef struct RTable RTable;

/****************
* STRUCT HASH
*****************/

typedef struct RHash {
    struct RBasic basic;
    struct st_table *tbl;
    int iter_lev;
    VALUE ifnone;
} RHash;

typedef struct st_hash_type {
    int (*compare)();
    int (*hash)();
} st_hash_type;

typedef struct st_table {
    struct st_hash_type *type;
    int num_bins;
    int num_entries;
    struct st_table_entry **bins;
} st_table;

typedef unsigned long st_data_t;

typedef struct st_table_entry st_table_entry;

struct st_table_entry {
    unsigned int hash;
    st_data_t key;
    st_data_t record;
    st_table_entry *next;
};

/***************************************************************
* CALCULATE MODE7
***************************************************************/

int EXPORT computeM7(const int dataReference, const int lightlineReference, const int paramsId) {

 // variables declaration
 RGSSBMINFO *lightlineBitmap = ((RGSSBITMAP*)(lightlineReference<<1))->bm->bminfo;
 DWORD *ptr;
 RArray *rArray;
 RTableData *rTableData;
 SWORD *tData;
 RTable *rTable;
 int cosAngle, sinAngle, altitude, pivot, slope, correction;
 int heightLimit, cosTheta, sinTheta, distProj, zoom;
 int xt, yt, ys, xs, a, yp, xp, y0, val_5, ypl, xr, yr, xMin, xMax, xp0, yMin, yMax;
 int xc, yc, val_1, val_2, val_3, val_4;
 DWORD lightLineRowSize;
 LPBYTE firstLightlineRow, lightlineData;
 int blue, green, red, alpha, lux, lux_b, lux_g, lux_r, lux_d;
 long xsize, ysize, oy, oz;

 // parameters initialization
 rArray = (RArray*)(paramsId << 1);
 ptr = rArray->ptr;
 cosAngle = *ptr;
 sinAngle = *(ptr + 1);
 altitude = *(ptr + 2) >> 1;
 pivot = *(ptr + 3) >> 1;
 slope = *(ptr + 4);
 correction = *(ptr + 5);
 heightLimit = *(ptr + 6) >> 1;
 cosTheta = *(ptr + 7);
 sinTheta = *(ptr + 8);
 distProj = *(ptr + 9) >> 1;
 zoom = *(ptr + 10) >> 1;
 xMin = *(ptr + 11) >> 1;
 xMax = *(ptr + 12) >> 1;
 yMin = *(ptr + 13) >> 1;
 yMax = *(ptr + 14) >> 1;
 if (*(ptr + 15) >> 1) {
  yMax = (yMax << 1) - yMin;
 }
 rTable = (RTable*)(dataReference << 1);
 rTableData = rTable->rTableData;
 tData = rTableData->data;
 xsize = rTableData->xsize;
 ysize = rTableData->ysize;

 // variables initialization
 a = xsize >> 1;
 lightLineRowSize = (lightlineBitmap->infoheader->biWidth) << 2;
 firstLightlineRow = (LPBYTE) lightlineBitmap->firstRow;
 xc = a;
 yc = pivot;
 val_4 = (altitude - distProj) * cosAngle;
 oz = xsize * ysize;

 // drawing
 lightlineData = firstLightlineRow;
 lux_b = lightlineData[0];
 lux_g = lightlineData[1];
 lux_r = lightlineData[2];
 lux_d = lightlineData[3];
 y0 = heightLimit;
 if (heightLimit < yMin) {y0 = yMin;}
 for (yt = y0; yt < yMax; yt++) {
  yp = (DIVISE((altitude * (yt - pivot)), (val_4 + (yt - pivot) * sinAngle >> 12)) * zoom >> 12) + pivot;
  ys = yp;
  val_1 = slope * yt + correction;
  val_2 = (ys - yc) * cosTheta;
  val_3 = (ys - yc) * sinTheta;
  // computing lightline
  ypl = pivot - yp;
  if (ypl >= 0) {
   lightlineData = firstLightlineRow + (yt << 2);
   lux = (lux_b * ypl) / 960;
   if (lux > 255) {lux = 255;}
   lightlineData[0] = lux;
   lux = (lux_g * ypl) / 960;
   if (lux > 255) {lux = 255;}
   lightlineData[1] = lux;
   lux = (lux_r * ypl) / 960;
   if (lux > 255) {lux = 255;}
   lightlineData[2] = lux;
   lightlineData[3] = lux_d;
  }
  oy = yt * xsize;
  for (xt = xMin; xt < a; xt++) {
   xp = zoom * DIVISE(((a - xt) << 18), val_1) >> 12;
   if (xt == xMin) {
	if (xMin) {
	 xp0 = zoom * DIVISE(a << 18, val_1) >> 12;
	} else {
	 xp0 = xp;
	}
    // relief coefficient
    val_5 = (a * sinAngle) / xp0;
	lightlineData = firstLightlineRow - lightLineRowSize + (yt << 2);
	lightlineData[0] = val_5 >> 8;
	lightlineData[1] = val_5 - (lightlineData[0] << 8);
	val_5 = (a << 12) / xp0;
	lightlineData[2] = val_5 >> 8;
	lightlineData[3] = val_5 - (lightlineData[0] << 8);
   }
   // left part
   xs = a - xp;
   // rotation
   yr = yc + (((xs - xc) * sinTheta + val_2) >> 12);
   xr = xc + (((xs - xc) * cosTheta - val_3) >> 12);
   *(tData + xt + oy) = xr;
   *(tData + xt + oy + oz) = yr;
   // right part
   xs = a + xp;
   // rotation
   yr = yc + (((xs - xc) * sinTheta + val_2) >> 12);
   xr = xc + (((xs - xc) * cosTheta - val_3) >> 12);
   *(tData + xsize - 1 - xt + oy) = xr;
   *(tData + xsize - 1 - xt + oy + oz) = yr;
  } // end xt
 } // end yt
 return 0;
}

/***************************************************************
* DRAW HEIGHTMAP
***************************************************************/

int EXPORT drawHeightmap(const int heightmapId, const int patternId,
							const int mapTilesetId, const int tilemapDataId, const int nbLayers) {

 // variables declaration
 RGSSBMINFO *patternBmp, *mapTilesetBmp;
 RTable *tilemapTable, *heightmapTable;
 RTableData *tilemapTableData, *heightmapTableData;
 SWORD *tilemapData, *heightmapData, *heightmapDataBush;
 int heightmapXsize, tilemapXsize, tilemapYsize, mapWidthPx, mapHeightPx, patternWidth, patternHeight;
 DWORD heightmapSize, patternRowSize;
 LPBYTE patternBegin, mapTilesetBegin;
 LPBYTE patternData1, patternData2, mapTilesetData;
 char xr, yr, l, nbBlocks; // n-Layers
 int xt, yt, xs, ys;
 long yc, xc;
 short tileIndex, xts, hGround, oGround, tGround; // n-Layers
 int yts;

 // variables initialization
 nbBlocks = nbLayers + 8 >> 2; // n-Layers
 patternBmp = ((RGSSBITMAP*) (patternId << 1))->bm->bminfo;
 mapTilesetBmp = ((RGSSBITMAP*) (mapTilesetId << 1))->bm->bminfo;
 heightmapTable = (RTable*) (heightmapId << 1);
 heightmapTableData = heightmapTable->rTableData;
 heightmapData = heightmapTableData->data;
 heightmapXsize = heightmapTableData->xsize;
 tilemapTable = (RTable*) (tilemapDataId << 1);
 tilemapTableData = tilemapTable->rTableData;
 tilemapData = tilemapTableData->data;
 tilemapXsize = tilemapTableData->xsize;
 tilemapYsize = tilemapTableData->ysize;
 mapWidthPx = tilemapXsize / (nbLayers + 1) << 5; // n-Layers
 mapHeightPx = tilemapYsize << 5;
 patternWidth = patternBmp->infoheader->biWidth;
 patternRowSize = patternWidth << 2;
 patternHeight = patternBmp->infoheader->biHeight;
 patternBegin = (LPBYTE) patternBmp->firstRow;
 mapTilesetBegin = (LPBYTE) mapTilesetBmp->firstRow;
 heightmapData += mapHeightPx * heightmapXsize;
 heightmapDataBush = heightmapData + mapHeightPx * mapWidthPx;

 // drawing
 for (yt = mapHeightPx; yt-->0;) {
  heightmapData -= heightmapXsize;
  heightmapDataBush -= mapWidthPx;
  yc = (yt * patternHeight * 10) / mapHeightPx;
  ys = yc / 10;
  yr = yc - 10 * ys;
  for (xt = mapWidthPx; xt-->0;) {
   xc = (xt * patternWidth * 10) / mapWidthPx;
   xs = xc / 10;
   xr = xc - 10 * xs;
   patternData1 = patternBegin - ys * patternRowSize + (xs << 2);
   if (ys != patternHeight - 1 && xs != patternWidth - 1) {
    patternData2 = patternBegin - (ys + 1) * patternRowSize + (xs << 2);
    hGround = ((10 - xr) * (10 - yr) * patternData1[0] + xr * (10 - yr) * patternData1[4] + (10 - xr) * yr * patternData2[0] + xr * yr * patternData2[4]) / 100;
   } else if (ys != patternHeight - 1) {
    patternData2 = patternBegin - (ys + 1) * patternRowSize + (xs << 2);
	hGround = ((10 - yr) * patternData1[0] + yr * patternData2[0]) / 10;
   } else if (xs != patternWidth - 1) {
	hGround = ((10 - xr) * patternData1[0] + xr * patternData1[4]) / 10;
   } else {
    hGround = patternData1[0];
   }
   tileIndex = *(tilemapData + ((yt >> 5) * tilemapXsize) + ((xt >> 5) * (nbLayers + 1))); // n-Layers
   xts = ((tileIndex - (tileIndex >> 3 << 3) << 5) + (xt - (xt >> 5 << 5))) * nbBlocks; // n-Layers
   yts = (tileIndex >> 3 << 5) + (yt - (yt >> 5 << 5));
   mapTilesetData = mapTilesetBegin - ((yts << 10) * nbBlocks) + (xts << 2); // n-Layers
   oGround = 0;
   for (l = 0; l < mapTilesetData[4 + nbLayers]; l++) {
    oGround += mapTilesetData[4 + l];
   }
    // n-Layers
   tGround = oGround;
   for (l = mapTilesetData[4 + nbLayers]; l < nbLayers; l++) {
    tGround += mapTilesetData[4 + l];
   }
   *(heightmapData + (xt << 1)) = hGround + tGround;
   *(heightmapDataBush + xt) = hGround + oGround;
  } // end for xt
 } // end for yt
 return 0;
}

/***************************************************************
* DRAW TEXTURESET
***************************************************************/

int EXPORT drawTextureset(const int textureHashId, const int colormapId, const int textureAutoId) {

 // variables declaration
 RGSSBMINFO *textureBmp, *colormapBmp, *textureAutoBmp;
 DWORD *ptr;
 RArray *rArray;
 RHash *textureHash;
 st_table *textureHashTable;
 st_table_entry *entry;
 LPBYTE textureBegin, colormapBegin, textureAutoBegin, textureData, colormapData, textureAutoData;
 int bin, tileNum, yt, i, j, k, textureWidth, textureHeight, tileValue, ox, oy, animNbr, animIndex;

 // variables initialization
 colormapBmp = ((RGSSBITMAP*) (colormapId << 1))->bm->bminfo;
 textureHash = (RHash*) (textureHashId << 1);
 textureHashTable = textureHash->tbl;
 colormapBegin = (LPBYTE) colormapBmp->firstRow;
 textureAutoBmp = ((RGSSBITMAP*) (textureAutoId << 1))->bm->bminfo;
 textureAutoBegin = (LPBYTE) textureAutoBmp->firstRow;

 // textureset drawing for each entry stored in the hashmap
 // hashkeys loop
 for (bin = textureHashTable->num_bins; bin-->0;) {
  entry = textureHashTable->bins[bin];
  // entries loop for a given hashkey
  for (;entry;) {
   tileNum = entry->key >> 1;
   yt = tileNum << 5;
   rArray = (RArray*) (entry->record);
   ptr = rArray->ptr;
   tileValue = *ptr >> 1;
   textureBmp = ((RGSSBITMAP*) *(ptr + 1))->bm->bminfo;
   animNbr = *(ptr + 2) >> 1;
   animIndex = *(ptr + 3) >> 1;
   if (!textureBmp) {continue;}
   textureWidth = textureBmp->infoheader->biWidth;
   textureHeight = textureBmp->infoheader->biHeight;
   textureBegin = (LPBYTE) textureBmp->firstRow;
   if (tileValue >= 384) {
    for (i = 32; i-->0;) {
     for (j = 32; j-->0;) {
	  textureData = textureBegin - (i * 5 * animNbr << 7) + (j << 2) + (animIndex * 5 << 7);
	  colormapData = colormapBegin - ((yt + i) * 5 << 7) + (j << 2);
	  if (textureData[2]) { // red
	   colormapData[0] = 32;
	  } else if (textureData[1]) { // green
       colormapData[0] = 64;
	  } else if (textureData[0]) { // blue
       colormapData[0] = 96;
	  } else { // black
       colormapData[0] = 128;
	  }
	  for (k = 5; k-->1;) {
	   if (k == 1 || k == 4) {
	    textureData = textureBegin - (i * 5 * animNbr << 7) + (j << 2) + (k << 7) + (animIndex * 5 << 7);
	   } else {
	    textureData = textureBegin - (i * 5 * animNbr << 7) + ((31 - j) << 2) + (k << 7) + (animIndex * 5 << 7);
	   }
       colormapData = colormapBegin - ((yt + j) * 5 << 7) + (i << 2) + (k << 7);
       colormapData[0] = textureData[0];
	   colormapData[1] = textureData[1];
	   colormapData[2] = textureData[2];
	   colormapData[3] = textureData[3];
	  } // end for k
     } // end for j
    } // end for i
   } else { // tileValue < 384 : autotile
    ox = tileValue % 8 << 5;
    oy = tileValue % 48 >> 3 << 5;
	for (i = 32; i-->0;) {
     for (j = 32; j-->0;) {
	  textureAutoData = textureAutoBegin - (i + oy << 10) + (j + ox << 2);
	  colormapData = colormapBegin - ((yt + i) * 5 << 7) + (j << 2);
	  if (textureAutoData[2]) { // red
	   colormapData[0] = 32;
	  } else if (textureAutoData[1]) { // green
       colormapData[0] = 64;
	  } else if (textureAutoData[0]) { // blue
       colormapData[0] = 96;
	  } else { // black
       colormapData[0] = 128;
	  }
	  for (k = 5; k-->1;) {
	   if (k == 1 || k == 4) {
	    textureData = textureBegin - (i * animNbr << 9) + (j << 2) + (k - 1 << 7) + (animIndex << 9);
	   } else {
	    textureData = textureBegin - (i * animNbr << 9) + ((31 - j) << 2) + (k - 1 << 7) + (animIndex << 9);
	   }
       colormapData = colormapBegin - ((yt + j) * 5 << 7) + (i << 2) + (k << 7);
       colormapData[0] = textureData[0];
	   colormapData[1] = textureData[1];
	   colormapData[2] = textureData[2];
	   colormapData[3] = textureData[3];
	  } // end for k
	 } // end for j
	} // end for i
   } // end if tileValue
   entry = entry->next;
  } // end for entry
} // end for bin
 return 0;
}

/***************************************************************
* APPLY LIGHTING EFFECT
***************************************************************/

int EXPORT applyLighting(const int heightmapId) {

 // variables declaration
 RTable *heightmapTable;
 RTableData *heightmapTableData;
 SWORD *tilemapData, *heightmapData;
 int xt, yt, dy, yd, dyPos, heightmapXsize, heightmapWidth, heightmapHeight;
 int dyRef, k, value, dist, length, shadow;
 char initDy, lumDesc;
 static const char valuesAsc[51] = {0, 45, 26, 18, 14, 11, 9, 8, 7, 6, 5, 5, 4, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};


 // variables initialization
 heightmapTable = (RTable*) (heightmapId << 1);
 heightmapTableData = heightmapTable->rTableData;
 heightmapData = heightmapTableData->data;
 heightmapXsize = heightmapTableData->xsize;
 heightmapWidth = heightmapXsize >> 1;
 heightmapHeight = (heightmapTableData->ysize << 1) / 3;
 
 // drawing
 for (yt = 0; yt < heightmapHeight; yt++) { // for each row
  dist = 0;
  initDy = 0;
  shadow = 0;
  lumDesc = 0;
  if (yt) {heightmapData += heightmapXsize;}
  for (xt = 0; xt < heightmapWidth; xt++) { // for each column
   dist++;
   dy = *(heightmapData + (xt << 1)) >> 3;
   if (initDy) {
	if (shadow) {
	 shadow--;
	 if (dy < shadow) {
	  *(heightmapData + (xt << 1) + 1) = -50;
	  continue;
	 } else {
	  shadow = 0;
	  dyRef = dy;
	  dist = 1;
	 }
	} // end if shadow
	if (dy > dyRef) {
	 if (dy - dyRef - 1) {
	  if (lumDesc) {
	   length = dist - 1;
	   if (length > 50) {length = 50;}
       value = -valuesAsc[length];
	   for (k = 1; k <= length; k++) {
	    *(heightmapData + (xt - dist + k << 1) + 1) = value;
	   }
	  }
	 } else { // dy - dyRef = 1
	  length = dist;
	  if (lumDesc) {
	   // do nothing
	  } else { // lumDesc = 0
	   if (length > 51) {length = 51;}
       value = valuesAsc[length - 1];
	   for (k = length; k-->1;) {
	    *(heightmapData + (xt - k << 1) + 1) = value;
	   }
	  }
	 } // end if dy - dyRef - 1
	 lumDesc = 0;
	 dyRef = dy;
	 dist = 1;
	} else if (dy < dyRef) {
	 if (lumDesc) {
	  length = dist - 1;
	  if (length > 50) {length = 50;}
      value = -valuesAsc[length];
	  for (k = 1; k <= length; k++) {
	   *(heightmapData + (xt - dist + k << 1) + 1) = value;
	  }
	  if (dyRef - dy - 1) {
	   shadow = dyRef;
	   *(heightmapData + (xt << 1) + 1) = -50;
	  } else { // dyRef - dy = 1
	   lumDesc = 1;
	  } // end if dy - dyRef - 1
	 } else { // lumDesc = 0
	  if (dyRef - dy - 1) {
	   shadow = dyRef;
	   *(heightmapData + (xt << 1) + 1) = -50;
	  } else { // dyRef - dy = 1
	   lumDesc = 1;
	  } // end if dy - dyRef - 1
	 } // end if lumDesc
	 dyRef = dy;
	 dist = 1;
	} // end if dy <> dyref
   } else { // initDy = 0
    dyRef = dy;
    initDy = 1;
   } // end if initDy
  } // end for xt
 } // end for yt
 return 0;
}

/***************************************************************
* DRAW TILESET
***************************************************************/

int EXPORT drawMapTileset(const int mapTilesetId, const int tilesetId,
						  const int heightsetId, const int tilemapHashId, 
						  const int autoTilesetsId, const int nbLayers) {

 // variables declaration
 RGSSBMINFO *mapTilesetBmp, *tilesetBmp, *heightsetBmp, *autoTilesetBmp, *autoHeightsetBmp;
 RHash *tilemapHash;
 st_table *tilemapHashTable;
 st_table_entry *entry;
 RArray *tilesData, *autoTilesets;
 DWORD *ptr, *autoPtr;
 LPBYTE mapTilesetBegin, tilesetBegin, heightsetBegin, autoTilesetBegin, autoHeightsetBegin;
 LPBYTE mapTilesetData, tilesetData, heightsetData, autoTilesetData, autoHeightsetData;
 int bin, tileNum, xt, yt, xs, ys, tileValue, layer, i, j, xtc, bush;
 char numAutoTileset, nbBlocks; // n-Layers

 // variables initialization
 nbBlocks = nbLayers + 8 >> 2; // n-Layers
 mapTilesetBmp = ((RGSSBITMAP*) (mapTilesetId << 1))->bm->bminfo;
 tilesetBmp = ((RGSSBITMAP*) (tilesetId << 1))->bm->bminfo;
 heightsetBmp = ((RGSSBITMAP*) (heightsetId << 1))->bm->bminfo;
 tilemapHash = (RHash*) (tilemapHashId << 1);
 tilemapHashTable = tilemapHash->tbl;
 mapTilesetBegin = (LPBYTE) mapTilesetBmp->firstRow;
 tilesetBegin = (LPBYTE) tilesetBmp->firstRow;
 heightsetBegin = (LPBYTE) heightsetBmp->firstRow;
 autoTilesets = (RArray*) (autoTilesetsId << 1);
 autoPtr = autoTilesets->ptr;

 // 3-Layers tile drawing for each entry stored in the hashmap
 // hashkeys loop
 for (bin = tilemapHashTable->num_bins; bin-->0;) {
  entry = tilemapHashTable->bins[bin];
  // entries loop for a given hashkey
  for (;entry;) {
   tileNum = entry->key >> 1;
   xt = tileNum - (tileNum >> 3 << 3) << 5; // n-Layers
   //xt = xtc * nbBlocks; // n-Layers
   yt = tileNum >> 3 << 5;
   tilesData = (RArray*) (entry->record);
   ptr = tilesData->ptr;
   bush = *(ptr + nbLayers) >> 1; // n-Layers
   // layers loop
   for (layer = nbLayers; layer-->0;) { // n-Layers
    tileValue = *(ptr + layer) >> 1;
	if (tileValue >= 384) {
	 // not an autotile
	 tileValue -= 384;
	 xs = tileValue - (tileValue >> 3 << 3) << 5;
	 ys = tileValue >> 3 << 5;
	 // drawing
	 for (i = 32; i-->0;) {
	  for (j = 32; j-->0;) {
	   mapTilesetData = mapTilesetBegin - ((yt + i << 10) * nbBlocks) + ((xt + j) * nbBlocks << 2); // n-Layers
	   heightsetData = heightsetBegin - (ys + i << 10) + (xs + j << 2);
	   mapTilesetData[4 + layer] = heightsetData[0];
	   mapTilesetData[4 + nbLayers] = bush; // n-Layers
	   if (!mapTilesetData[3]) {
        tilesetData = tilesetBegin - (ys + i << 10) + (xs + j << 2);
		if (tilesetData[3]) {
         mapTilesetData[0] = tilesetData[0];
		 mapTilesetData[1] = tilesetData[1];
		 mapTilesetData[2] = tilesetData[2];
		 mapTilesetData[3] = 255;
		} // end if tilesetData[3]
	   } // end if mapTilesetData[3]
	  } // end for j
	 } // end for i
	} else {
	 // autotile
	 numAutoTileset = tileValue / 48;
	 if (numAutoTileset-->0) {
	  autoTilesetBmp = ((RGSSBITMAP*) (*(autoPtr + numAutoTileset)))->bm->bminfo;
	  autoTilesetBegin = (LPBYTE) autoTilesetBmp->firstRow;
	  autoHeightsetBmp = ((RGSSBITMAP*) (*(autoPtr + numAutoTileset + 7)))->bm->bminfo;
	  autoHeightsetBegin = (LPBYTE) autoHeightsetBmp->firstRow;
	  tileValue %= 48;
	  xs = tileValue - (tileValue >> 3 << 3) << 5;
	  ys = tileValue >> 3 << 5;
	  // drawing
	  for (i = 32; i-->0;) {
	   for (j = 32; j-->0;) {
	    mapTilesetData = mapTilesetBegin - ((yt + i << 10) * nbBlocks) + ((xt + j) * nbBlocks << 2); // n-Layers
	    autoHeightsetData = autoHeightsetBegin - (ys + i << 10) + (xs + j << 2);
	    mapTilesetData[4 + layer] = autoHeightsetData[0];
		mapTilesetData[4 + nbLayers] = bush; // n-Layers
	    if (!mapTilesetData[3]) {
         autoTilesetData = autoTilesetBegin - (ys + i << 10) + (xs + j << 2);
		 if (autoTilesetData[3]) {
          mapTilesetData[0] = autoTilesetData[0];
		  mapTilesetData[1] = autoTilesetData[1];
		  mapTilesetData[2] = autoTilesetData[2];
		  mapTilesetData[3] = 255;
		 } // end if autoTilesetData[3]
	    } // end if mapTilesetData[3]
	   } // end for j
	  } // end for i
	 } // end if numAutoTileset
	} // end if tileValue
   } // end for layer
   entry = entry->next;
  } // end for entry
 } // end for bin
 return 0;
}

/***************************************************************
* REFRESH TILESET FOR ANIMATIONS
***************************************************************/

int EXPORT refreshMapTileset(const int mapTilesetId, const int tilesetId,
								  const int tilemapHashId, const int autoTilesetsId, const int nbLayers) {

 // variables declaration
 RGSSBMINFO *mapTilesetBmp, *tilesetBmp, *autoTilesetBmp;
 RHash *tilemapHash;
 st_table *tilemapHashTable;
 st_table_entry *entry;
 RArray *tilesData, *autoTilesets;
 DWORD *ptr, *autoPtr;
 LPBYTE mapTilesetBegin, tilesetBegin, autoTilesetBegin;
 LPBYTE mapTilesetData, tilesetData, autoTilesetData;
 int bin, tileNum, xt, yt, xs, ys, tileValue, layer, i, j, xtc;
 char numAutoTileset, nbBlocks; // n-Layers

 // variables initialization
 nbBlocks = nbLayers + 8 >> 2; // n-Layers
 mapTilesetBmp = ((RGSSBITMAP*) (mapTilesetId << 1))->bm->bminfo;
 tilesetBmp = ((RGSSBITMAP*) (tilesetId << 1))->bm->bminfo;
 tilemapHash = (RHash*) (tilemapHashId << 1);
 tilemapHashTable = tilemapHash->tbl;
 mapTilesetBegin = (LPBYTE) mapTilesetBmp->firstRow;
 tilesetBegin = (LPBYTE) tilesetBmp->firstRow;
 autoTilesets = (RArray*) (autoTilesetsId << 1);
 autoPtr = autoTilesets->ptr;

 // 3-Layers tile drawing for each entry stored in the hashmap
 // hashkeys loop
 for (bin = tilemapHashTable->num_bins; bin-->0;) {
  entry = tilemapHashTable->bins[bin];
  // entries loop for a given hashkey
  for (;entry;) {
   tileNum = entry->key >> 1;
   xt = tileNum - (tileNum >> 3 << 3) << 5; // n-Layers
   //xt = xtc * nbBlocks; // n-Layers
   yt = tileNum >> 3 << 5;
   tilesData = (RArray*) (entry->record);
   ptr = tilesData->ptr;
   // layers loop
   for (layer = 0; layer < nbLayers; layer++) { // n-Layers
    tileValue = *(ptr + layer) >> 1;
	if (tileValue >= 384) {
	 // not an autotile
	 tileValue -= 384;
	 xs = tileValue - (tileValue >> 3 << 3) << 5;
	 ys = tileValue >> 3 << 5;
	 // drawing
	 for (i = 32; i-->0;) {
	  for (j = 32; j-->0;) {
	   mapTilesetData = mapTilesetBegin - ((yt + i << 10) * nbBlocks) + ((xt + j) * nbBlocks << 2); // n-Layers
       tilesetData = tilesetBegin - (ys + i << 10) + (xs + j << 2);
	   if (tilesetData[3]) {
        mapTilesetData[0] = tilesetData[0];
		mapTilesetData[1] = tilesetData[1];
		mapTilesetData[2] = tilesetData[2];
		mapTilesetData[3] = tilesetData[3];
	   } // end if tilesetData[3]
	  } // end for j
	 } // end for i
	} else {
	 // autotile
	 numAutoTileset = tileValue / 48;
	 if (numAutoTileset-->0) {
	  autoTilesetBmp = ((RGSSBITMAP*) (*(autoPtr + numAutoTileset)))->bm->bminfo;
	  autoTilesetBegin = (LPBYTE) autoTilesetBmp->firstRow;
	  tileValue %= 48;
	  xs = tileValue - (tileValue >> 3 << 3) << 5;
	  ys = tileValue >> 3 << 5;
	  // drawing
	  for (i = 32; i-->0;) {
	   for (j = 32; j-->0;) {
	    mapTilesetData = mapTilesetBegin - ((yt + i << 10) * nbBlocks) + ((xt + j) * nbBlocks << 2); // n-Layers
        autoTilesetData = autoTilesetBegin - (ys + i << 10) + (xs + j << 2);
		if (autoTilesetData[3]) {
         mapTilesetData[0] = autoTilesetData[0];
		 mapTilesetData[1] = autoTilesetData[1];
		 mapTilesetData[2] = autoTilesetData[2];
		 mapTilesetData[3] = autoTilesetData[3];
		} // end if autoTilesetData[3]
	   } // end for j
	  } // end for i
	 } // end if numAutoTileset
	} // end if tileValue
   } // end for layer
   entry = entry->next;
  } // end for entry
 } // end for bin
 return 0;
}

/***************************************************************
* RENDER HM7
***************************************************************/

typedef signed int SINT;

int EXPORT renderHM7(const int paramsId, const int varsId, const int rSurfacesId, const int nbLayers) {

 // types declaration
 RGSSBMINFO *screenBitmap, *lightlineBitmap, *mapTilesetBitmap, *colormapBmp;
 DWORD *ptr, *sPtr;
 RArray *rArraySurfaces, *rArraySurface, *rArrayParams, *rArrayVars;
 RTable *rTable, *tilemapTable, *heightmapTable;
 RTableData *rTableData, *tilemapTableData, *heightmapTableData;
 SWORD *tData, *tilemapData, *heightmapData, *ptrTileIndex;
 int xt, yt, x_quad, dy, new_y, h_coeff, y0, ody, y0min;
 int displayX, displayY, xMin, xMax, yMin, yMax, yMaxDraw;
 int ys, xs, a, yp, xp, yd, loopX, loopY, heightLimit, ym, h3, h2, h1, d2, d1;
 int screenWidth, screenHeight, mapWidthPx, mapHeightPx, x0;
 DWORD screenRowSize, lightLineRowSize;
 LPBYTE firstScreenRow, firstHeightmapRow, firstLightlineRow;
 LPBYTE screenData, lightlineData;
 int blue, green, red, alpha, lux_b, lux_g, lux_r, lux_d;
 LPBYTE firstMapTilesetRow, mapTilesetData, colormapBegin, colormapData;
 short tileIndex, xts, xtc, pos, oColor;
 int yts, ytc, oShadow, odyh;
 char filter, step, l3, l2, l1, shadow, xsr, ysr, init3, init2, init1, top, ground, cam, noBlack;
 long xsize, ysize, ysizeReal, oy, oz, tilemapXsize, tilemapYsize, heightmapXsize;
 int oCamera, oScrY, rYt;
 char *initA, *lA;
 int *hA, *dA;
 //char initA[nbLayers], lA[nbLayers]; // n-Layers
 //int hA[nbLayers], dA[nbLayers]; // n-Layers
 int itLayer, totHA; // n-Layers
 char nbBlocks; // n-Layers

 // surfaces 
 char type, sNext, sDraw, sFirstX, sBlend, blend;
 short screenX, screenY, sCount, sOpacity;
 long sNumber;
 int sWidth, sHeight, sRealWidth, sRealHeight, sX, sY, sXt, sOdy, sDy, sXinit, sXc;
 RGSSBMINFO *sBitmap, *sScreenBitmap;
 LPBYTE firstSRow, sData, firstSScreenRow, sScreenData;
 DWORD sRowSize, sScreenRowSize;
 char sInverse;
 int sDispWidth, sDispOffset, sDx, sDh, sSlope, sH0, sFYt, sFYth, sFh, sHinit, sXi, sHend, sC1, sC2;
 int sCmin, sCmax, sCsl, sHbase, sCminHT0, sCmaxHT0, sCslHT0, sCminHT2, sCmaxHT2, sCslHT2;
 short screenX1, screenY1, screenX2, screenY2, sXmax, sXmin, sVFading;
 int h;
 LPBYTE sLightlineData;
 int dx1, dx2, sHMax;
 int sLux_b, sLux_g, sLux_r, sLux_d;
 char sInitZoomData;

 // initialization
 // parameters
 initA = (char *) malloc(nbLayers * sizeof(char));
 lA = (char *) malloc(nbLayers * sizeof(char));
 hA = (int *) malloc(nbLayers * sizeof(int));
 dA = (int *) malloc(nbLayers * sizeof(int));
 nbBlocks = nbLayers + 8 >> 2; // n-Layers
 rArrayParams = (RArray*) (paramsId << 1);
 ptr = rArrayParams->ptr;
 screenBitmap = ((RGSSBITMAP*) *ptr)->bm->bminfo;
 rTable = (RTable*) *(ptr + 1);
 rTableData = rTable->rTableData;
 tData = rTableData->data;
 xsize = rTableData->xsize;
 ysizeReal = rTableData->ysize;
 lightlineBitmap = ((RGSSBITMAP*) *(ptr + 2))->bm->bminfo;
 heightmapTable = (RTable*) *(ptr + 3);
 heightmapTableData = heightmapTable->rTableData;
 heightmapData = heightmapTableData->data;
 heightmapXsize = heightmapTableData->xsize;
 mapTilesetBitmap = ((RGSSBITMAP*) *(ptr + 4))->bm->bminfo;
 tilemapTable = (RTable*) *(ptr + 5);
 tilemapTableData = tilemapTable->rTableData;
 tilemapData = tilemapTableData->data;
 tilemapXsize = tilemapTableData->xsize;
 tilemapYsize = tilemapTableData->ysize;
 mapWidthPx = tilemapXsize / (nbLayers + 1) << 5; // n-Layers
 mapHeightPx = tilemapYsize << 5;
 colormapBmp = ((RGSSBITMAP*) *(ptr + 6))->bm->bminfo;
 loopX = *(ptr + 7) >> 1;
 loopY = *(ptr + 8) >> 1;
 cam = *(ptr + 9) >> 1;
 sScreenBitmap = ((RGSSBITMAP*) *(ptr + 10))->bm->bminfo;
 if (*(ptr + 11) >> 1) {
  ysize = ysizeReal >> 1;
 } else {
  ysize = ysizeReal;
 }
 noBlack = *(ptr + 12) >> 1;
 xMin = *(ptr + 13) >> 1;
 xMax = *(ptr + 14) >> 1;
 yMin = *(ptr + 15) >> 1;
 yMaxDraw = *(ptr + 16) >> 1;
 if (*(ptr + 11) >> 1) {
  yMax = (yMaxDraw << 1) - yMin;
 } else {
  yMax = yMaxDraw;
 }
 // variables
 rArrayVars = (RArray*) (varsId << 1);
 ptr = rArrayVars->ptr;
 heightLimit = *ptr >> 1;
 displayX = *(ptr + 1);
 displayY = *(ptr + 2);
 filter = *(ptr + 3) >> 1;
 oScrY = *(ptr + 4) >> 1;

 // surfaces
 rArraySurfaces = (RArray*) (rSurfacesId << 1);
 sNumber = rArraySurfaces->len;
 if (sNumber > 0) {
  ptr = rArraySurfaces->ptr;
  rArraySurface = (RArray*) (*ptr);
  sPtr = rArraySurface->ptr;
  type = *sPtr >> 1;
  screenX1 = *(sPtr + 1) >> 1;
  screenY1 = *(sPtr + 2) >> 1;
  screenX2 = *(sPtr + 3) >> 1;
  screenY2 = *(sPtr + 4) >> 1;
  sInverse = *(sPtr + 5) >> 1;
  sBitmap = ((RGSSBITMAP*) *(sPtr + 6))->bm->bminfo;
  sDh = *(sPtr + 7) >> 1;
  sBlend = *(sPtr + 8) >> 1;
  sDispWidth = *(sPtr + 9) >> 1;
  sDispOffset = *(sPtr + 10) >> 1;
  firstSRow = (LPBYTE) sBitmap->firstRow;
  sWidth = sBitmap->infoheader->biWidth;
  sHeight = sBitmap->infoheader->biHeight;
  sRowSize = sWidth << 2;
  sCount = 1;
  sNext = 1;
  sDraw = 0;
  sFirstX = 0;
 } else {
  sNext = 0;
  sDraw = 0;
 }

 // local variables initialization
 screenWidth = screenBitmap->infoheader->biWidth;
 a = screenWidth >> 1;
 screenHeight = screenBitmap->infoheader->biHeight;
 screenRowSize = screenWidth << 2;
 sScreenRowSize = screenRowSize << 1;
 lightLineRowSize = (lightlineBitmap->infoheader->biWidth) << 2;
 firstScreenRow = (LPBYTE) screenBitmap->firstRow;
 firstSScreenRow = (LPBYTE) sScreenBitmap->firstRow;
 firstLightlineRow = (LPBYTE) lightlineBitmap->firstRow;
 colormapBegin = (LPBYTE) colormapBmp->firstRow;
 firstMapTilesetRow = (LPBYTE) mapTilesetBitmap->firstRow;
 if (filter == 0) {
  x0 = 0;
  step = 1;
 } else if (filter == 1) {
  x0 = 0;
  step = 2;
 } else {
  x0 = 1;
  step = 2;
 }
 oz = xsize * ysizeReal;
 top = 0;
 oCamera = 0;

 // drawing
 if (heightLimit > yMin) {
  y0 = heightLimit;
 } else {
  y0 = yMin;
 }
 sLightlineData = firstLightlineRow - lightLineRowSize + (yMax - 1 << 2);
 sCmax = (sLightlineData[0] << 8) + sLightlineData[1];
 sCmaxHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
 sCmaxHT0 = sCmax;
 sLightlineData = firstLightlineRow - lightLineRowSize + (y0 << 2);
 sCmin = (sLightlineData[0] << 8) + sLightlineData[1];
 sCsl = (sCmax - sCmin);
 sCminHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
 sCslHT2 = (sCmaxHT2 - sCminHT2);
 sCminHT0 = sCmin;
 sCslHT0 = sCsl;
 for (yt = yMax - 1; yt >= y0; yt--) { // for each row (from the bottom to the top)
  rYt = yt + oScrY;
  lightlineData = firstLightlineRow + (yt << 2);
  lux_b = lightlineData[0];
  lux_g = lightlineData[1];
  lux_r = lightlineData[2];
  lux_d = lightlineData[3];
  lightlineData = firstLightlineRow - lightLineRowSize + (yt << 2);
  h_coeff = (lightlineData[0] << 8) + lightlineData[1];
  oy = yt * xsize;



   sInitZoomData = 0;
   
   /**********************
   *    PRE-SURFACES
   **********************/
   if (!(yMax - 1 - yt)) {
   for (;sNext && yt <= screenY1;) {
	 // draw surface

	 if (!sInitZoomData) {
	  sLightlineData = firstLightlineRow - lightLineRowSize + (yMax - 1 << 2);
      sCmax = (sLightlineData[0] << 8) + sLightlineData[1];
      sCmaxHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
      sCmaxHT0 = sCmax;
      sLightlineData = firstLightlineRow - lightLineRowSize + (y0 << 2);
      sCmin = (sLightlineData[0] << 8) + sLightlineData[1];
      sCsl = (sCmax - sCmin);
      sCminHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
      sCslHT2 = (sCmaxHT2 - sCminHT2);
      sCminHT0 = sCmin;
      sCslHT0 = sCsl;
	  sInitZoomData = 1;
	 }
     sDx = screenX2 - screenX1;
	 if (sDx) {
	 sDy = screenY1 - screenY2;
     sSlope = (sDy << 7) / sDx;
     if (screenX2 > xMax) {
	  sXmax = xMax;
	 } else {
	  sXmax = screenX2;
	 }
	 if (x0) {
	  if (screenX1 & 1) {
	   sXmin = screenX1;
	  } else {
	   sXmin = screenX1 + 1;
	  }
	 } else {
	  if (screenX1 & 1) {
	   sXmin = screenX1 + 1;
	  } else {
	   sXmin = screenX1;
	  }
	 }
	 if (sXmin >= xMax) {
	  sXmin = xMax - 1;
	 } else if (sXmin < xMin) {
	  sXmin = xMin + x0;
	 }
	 if (screenY1 >= yMax) {
	  sC1 = sCmin + (sCsl * (screenY1 - y0)) / (yMax - 1 - y0);
	  if (screenY2 < 0) {
	   sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
	  } else {
	   if (screenY2 >= yMax) {
	    sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
	   } else {
	    sLightlineData = firstLightlineRow - lightLineRowSize + (screenY2 << 2);
	    sC2 = (sLightlineData[0] << 8) + sLightlineData[1];
	   }
	  }
	 } else {
	  sLightlineData = firstLightlineRow - lightLineRowSize + (screenY1 << 2);
	  sC1 = (sLightlineData[0] << 8) + sLightlineData[1];
	  if (screenY2 < 0) {
	   sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
	  } else {
	   sLightlineData = firstLightlineRow - lightLineRowSize + (screenY2 << 2);
	   sC2 = (sLightlineData[0] << 8) + sLightlineData[1];
	  }
	 }
	 if (!sC1) {sC1 = 1;}
	 if (!sC2) {sC2 = 1;}

	 // for each column
	 for (sXt = sXmin; sXt < sXmax; sXt+=step) {
	  if (sInverse) {
	   sH0 = (screenX2 - 1 - sXt) * sSlope >> 7;
	   dx1 = (screenX2 - 1 - sXt << 12) / sC1;
	   dx2 = (sXt - screenX1 << 12) / sC2;
	  } else {
	   sH0 = (sXt - screenX1) * sSlope >> 7;
	   dx1 = (sXt - screenX1 << 12) / sC1;
	   dx2 = (screenX2 - 1 - sXt << 12) / sC2;
	  } // end if sInverse
	  sH0 = sH0 - (screenY1 - yt);
	  // calculate column x-coordinate
	  if (rYt - sH0 < yMin) {continue;} // out of the screen : top
	  
	  if (!(dx1 + dx2)) {continue;}
	  if (sInverse) {
	   sX = sDispOffset + (sDispWidth * dx2) / (dx1 + dx2) << 2;
	  } else {
	   sX = sDispOffset + (sDispWidth * dx1) / (dx1 + dx2) << 2;
	  }
	  if (sX < 0 || sX >= sRowSize) {continue;}
	  // get zoom factor
	  if (sH0 < 0) { // yt < screenY1 - out of the screen : bottom
       sLux_b = 0;
       sLux_g = 0;
       sLux_r = 0;
       sLux_d = 0;
	   if (type) {
	    sFYt = sCminHT2 + (sCslHT2 * (yt - sH0 - y0)) / (yMax - 1 - y0);
	   } else {
	    sFYt = sCmin + (sCsl * (yt - sH0 - y0)) / (yMax - 1 - y0);
	   }
	   sFYth = sCminHT0 + (sCslHT0 * (yt - sH0 - y0)) / (yMax - 1 - y0);
	   sVFading = 0;
	   sHbase = 0;
	  } else {
	   sLightlineData = firstLightlineRow + (yt - sH0 << 2);
       sLux_b = sLightlineData[0];
       sLux_g = sLightlineData[1];
       sLux_r = sLightlineData[2];
       sLux_d = sLightlineData[3];
	   sLightlineData -= lightLineRowSize;
	   sFYt = (sLightlineData[type] << 8) + sLightlineData[type + 1];
	   sFYth = (sLightlineData[0] << 8) + sLightlineData[1];
	   sVFading = sLightlineData[2];
	   sHbase = sH0;
	  }
	  sH0 += (sDh * sFYth >> 15);
	  if (sH0 < 0) {
	   sHend = 0;
	  } else {
	   sHend = sH0;
	  }
	  if (rYt - sH0 < yMin) {continue;} // out of the screen : top
	  //sRealHeight = (sHeight * screenWidth >> 7) * sFYt >> 15;
      sRealHeight = sHeight * sFYt >> 12;
	  if (sRealHeight < 2) {continue;}
	  
	  if (rYt - sRealHeight - sH0 < yMin) {
	   sHinit = rYt - yMin;
	  } else {
	   sHinit = sRealHeight + sH0;
	  }
	  sFh = ((sHeight - 1) << 10) / (sRealHeight - 1);

      if (yt == yMax - 1) {
       sHMax = ysize + oScrY;
	  } else {
	   sLightlineData = firstLightlineRow - (lightLineRowSize << 1) + (sXt << 2);
       sHMax = (sLightlineData[0] << 8) + sLightlineData[1];
	  }

	  // draw column
      for (h = sHinit; h-->sHend;) {
	   if (rYt - h > sHMax) {break;}
	   if (rYt - h > yMaxDraw - 1) {break;}
	   sData = firstSRow - (sHeight - 1 - ((h - sH0) * sFh >> 10)) * sRowSize + sX;
	   if (sData[3]) {
		sScreenData = firstSScreenRow - (rYt - h) * sScreenRowSize + (sXt << 3);
		//if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 && sScreenData[2] + sScreenData[3] + 2 >= rYt - sHend) {
		if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255) {
		 // opaque surface
		 continue;
		}
		blue = sData[0];
	    green = sData[1];
	    red = sData[2];
		alpha = sData[3];
		if (sLux_d) {
	     blue += sLux_b;
	     green += sLux_g;
	     red += sLux_r;
	     if (blue > 255) {blue = 255;}
	     if (green > 255) {green = 255;}
	     if (red > 255) {red = 255;}
	   } else { // sLux_d == 0
	     blue -= sLux_b;
	     green -= sLux_g;
	     red -= sLux_r;
	    } // end if sLux_d
		if (sScreenData[0] && (sBlend || sData[3] < 255 || sScreenData[2] + sScreenData[3] >= rYt - sHend)) {
	     // surface translucide
	     blend = sScreenData[1];
	     sOpacity = sScreenData[7];
	     if (!blend) {
          blue = blue * (255 - sOpacity) + sScreenData[4] * sOpacity >> 8;
          green = green * (255 - sOpacity) + sScreenData[5] * sOpacity >> 8;
          red = red * (255 - sOpacity) + sScreenData[6] * sOpacity >> 8;
	     } else if (blend == 1) {
	      blue = blue + (sScreenData[4] * sOpacity >> 8);
	      green = green + (sScreenData[5] * sOpacity >> 8);
	      red = red + (sScreenData[6] * sOpacity >> 8);
	      if (blue > 255) {blue = 255;}
	      if (green > 255) {green = 255;}
	      if (red > 255) {red = 255;}
	     } else if (blend == 2) {
	      blue = blue - (sScreenData[4] * sOpacity >> 8);
	      green = green - (sScreenData[5] * sOpacity >> 8);
	      red = red - (sScreenData[6] * sOpacity >> 8);
		 }
		 alpha = ~((char) (((255 - alpha) * (255 - sScreenData[7])) / 255));
	    } // end if surface translucide
	    if (blue < 0) {blue = 0;}
	    if (green < 0) {green = 0;}
	    if (red < 0) {red = 0;}
		sScreenData[0] = 1;
	    sScreenData[1] = sBlend;
		if (rYt - sHbase > 510)  {
	     sScreenData[2] = 255;
         sScreenData[3] = 255;
	    } else if (rYt - sHbase > 255)  {
	     sScreenData[2] = rYt - sHbase - 255;
		 sScreenData[3] = 255;
	    } else {
	     sScreenData[2] = 0;
		 sScreenData[3] = rYt - sHbase;
	    }
	    sScreenData[4] = blue;
	    sScreenData[5] = green;
	    sScreenData[6] = red;
	    sScreenData[7] = alpha;
	   } // end if sData[3]
	  } // end for h
	 } // end for sXt
	} // end if dx
	// prepare the next surface
	if (sCount < sNumber) {
	 rArraySurface = (RArray*)(*(ptr + sCount));
     sPtr = rArraySurface->ptr;
     type = *sPtr >> 1;
     screenX1 = *(sPtr + 1) >> 1;
     screenY1 = *(sPtr + 2) >> 1;
	 screenX2 = *(sPtr + 3) >> 1;
     screenY2 = *(sPtr + 4) >> 1;
	 sInverse = *(sPtr + 5) >> 1;
     sBitmap = ((RGSSBITMAP*) *(sPtr + 6))->bm->bminfo;
	 sDh = *(sPtr + 7) >> 1;
	 sBlend = *(sPtr + 8) >> 1;
	 sDispWidth = *(sPtr + 9) >> 1;
     sDispOffset = *(sPtr + 10) >> 1;
     firstSRow = (LPBYTE) sBitmap->firstRow;
     sWidth = sBitmap->infoheader->biWidth;
     sHeight = sBitmap->infoheader->biHeight;
     sRowSize = sWidth << 2;
	 sCount++;
	} else {
	 sNext = 0;
    } // end if sCount < sNumber
   // end surfaces
   } // end for
   }




  for (xt = xMin + x0; xt < xMax; xt += step) { // for each column of the current row
   lightlineData = firstLightlineRow - (lightLineRowSize << 1) + (xt << 2);
   if (yt == yMax - 1) {
    ym = ysize + oScrY;
	lightlineData[0] = ym >> 8;
    lightlineData[1] = ym - (lightlineData[0] << 8);
   } else {
    ym = (lightlineData[0] << 8) + lightlineData[1];
   }
   xs = *(tData + xt + oy) + displayX;
   ys = *(tData + xt + oy + oz) + displayY;
   if (!loopX) {
    if (xs >= mapWidthPx || xs < 0) {
	 if (rYt < ym) {
	  if (rYt < yMaxDraw) {
	   screenData = firstScreenRow - rYt * screenRowSize + (xt << 2);
	   sScreenData = firstSScreenRow - rYt * sScreenRowSize + (xt << 3);
	   if (sScreenData[0]) {
	    // surface
		if (!sScreenData[1] && sScreenData[7] == 255) {
		 // surface opaque
         blue = sScreenData[4];
	     green = sScreenData[5];
	     red = sScreenData[6];
		} else {
		 // surface translucide ou avec un mode d'opacité
		 blend = sScreenData[1];
         if (blend == 2) {
		  // mode obscurci
	      blue = 0;
	      green = 0;
	      red = 0;
	     } else { // blend = 1
		  // mode éclairci
	      sOpacity = sScreenData[7];
	      blue = sScreenData[4] * sOpacity >> 8;
	      green = sScreenData[5] * sOpacity >> 8;
	      red = sScreenData[6] * sOpacity >> 8;
	     } // end if blend
		} // end if surface opaque/translucide
	    screenData[0] = blue;
	    screenData[1] = green;
	    screenData[2] = red;
	    screenData[3] = sScreenData[7];
		sScreenData[0] = 0;
		lightlineData[0] = rYt >> 8;
        lightlineData[1] = rYt - (lightlineData[0] << 8);
		continue;
	   } // end if surface
       screenData[3] = 0;
	  } // end if rYt < ysize
	  lightlineData[0] = rYt >> 8;
      lightlineData[1] = rYt - (lightlineData[0] << 8);
	 }
	 continue;
	}
   } else {
	if (xs >= mapWidthPx) {xs -= mapWidthPx * (xs / mapWidthPx);}
	else if (xs < 0) {xs -= mapWidthPx * (xs / mapWidthPx - 1);}
   }
   if (!loopY) {
	if (ys >= mapHeightPx || ys < 0) {
	 if (rYt < ym) {
	  if (rYt < yMaxDraw) {
	   screenData = firstScreenRow - rYt * screenRowSize + (xt << 2);
	   sScreenData = firstSScreenRow - rYt * sScreenRowSize + (xt << 3);
	   if (sScreenData[0]) {
	    // surface
		if (!sScreenData[1] && sScreenData[7] == 255) {
		 // surface opaque
         blue = sScreenData[4];
	     green = sScreenData[5];
	     red = sScreenData[6];
		} else {
		 // surface translucide ou avec un mode d'opacité
		 blend = sScreenData[1];
         if (blend == 2) {
		  // mode obscurci
	      blue = 0;
	      green = 0;
	      red = 0;
	     } else { // blend = 1
		  // mode éclairci
	      sOpacity = sScreenData[7];
	      blue = sScreenData[4] * sOpacity >> 8;
	      green = sScreenData[5] * sOpacity >> 8;
	      red = sScreenData[6] * sOpacity >> 8;
	     } // end if blend
		} // end if surface opaque/translucide
	    screenData[0] = blue;
	    screenData[1] = green;
	    screenData[2] = red;
	    screenData[3] = sScreenData[7];
		sScreenData[0] = 0;
		lightlineData[0] = rYt >> 8;
        lightlineData[1] = rYt - (lightlineData[0] << 8);
		continue;
	   } // end if surface
       screenData[3] = 0;
	  } // end if rYt < yMaxDraw
	  lightlineData[0] = rYt >> 8;
      lightlineData[1] = rYt - (lightlineData[0] << 8);
	 } // end if rYt < ym
	 continue;
	}
   } else {
	if (ys >= mapHeightPx) {ys -= mapHeightPx * (ys / mapHeightPx);}
	else while (ys < 0) {ys += mapHeightPx;}
   }
   ptrTileIndex = tilemapData + ((ys >> 5) * tilemapXsize) + ((xs >> 5)  * (nbLayers + 1)); // n-Layers
   tileIndex = *ptrTileIndex; // tilemap * 4 // * (nbLayers + 1)
   for (itLayer = nbLayers; itLayer-->0;) {
    initA[itLayer] = 0;
   }
   xts = ((tileIndex - (tileIndex >> 3 << 3) << 5) + (xs - (xs >> 5 << 5))) * nbBlocks; // n-Layers
   yts = (tileIndex >> 3 << 5) + (ys - (ys >> 5 << 5));
   xsr = xs - (xs >> 5 << 5);
   ysr = ys - (ys >> 5 << 5);
   
   mapTilesetData = firstMapTilesetRow - ((yts << 10) * nbBlocks) + (xts << 2); // n-Layers
   dy = *(heightmapData + (xs << 1) + ys * heightmapXsize) * h_coeff >> 15;
   oShadow = *(heightmapData + (xs << 1) + ys * heightmapXsize + 1);
   shadow = (oShadow != 0);
   totHA = 0;
   for (itLayer = nbLayers; itLayer-->0;) {
    hA[itLayer] = mapTilesetData[4 + itLayer] * h_coeff >> 15;
	totHA += hA[itLayer];
	dA[itLayer] = totHA;
	lA[itLayer] = mapTilesetData[4 + itLayer] >> 3;
   }
   alpha = mapTilesetData[3];
   if (dy > rYt - yMin) {
    odyh = dy - rYt + yMin;
    dy = rYt - yMin;
   } else {
    odyh = 0;
   }
   if (!(yt + 1 - ysize)) {
	if (cam > 1) {
	 ody = dy;
	} else {
	 ody = dy - totHA; // n-Layers
	}
	if (ody > oCamera) {oCamera = ody;}
   }
   ody = rYt - dy;


   /**********************
   *    SURFACES
   **********************/
   
   if (sNext && yt <= screenY1 && xt >= screenX1) {
	if (xt < screenX2) {
	 // draw surface
	 if (!sInitZoomData) {
	  sLightlineData = firstLightlineRow - lightLineRowSize + (yMax - 1 << 2);
      sCmax = (sLightlineData[0] << 8) + sLightlineData[1];
      sCmaxHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
      sCmaxHT0 = sCmax;
      sLightlineData = firstLightlineRow - lightLineRowSize + (y0 << 2);
      sCmin = (sLightlineData[0] << 8) + sLightlineData[1];
      sCsl = (sCmax - sCmin);
      sCminHT2 = (sLightlineData[2] << 8) + sLightlineData[3];
      sCslHT2 = (sCmaxHT2 - sCminHT2);
      sCminHT0 = sCmin;
      sCslHT0 = sCsl;
	  sInitZoomData = 1;
	 }


     sDx = screenX2 - screenX1;
	 if (sDx) {
		 sDy = screenY1 - screenY2;
		 sSlope = (sDy << 7) / sDx;
		 if (screenX2 > xMax) {
		  sXmax = xMax;
		 } else {
		  sXmax = screenX2;
		 }
		 sXmin = xt;
		 if (screenY1 >= yMax) {
		  sC1 = sCmin + (sCsl * (screenY1 - y0)) / (yMax - 1 - y0);
		  if (screenY2 < 0) {
		   sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
		  } else {
		   if (screenY2 >= yMax) {
			sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
		   } else {
			sLightlineData = firstLightlineRow - lightLineRowSize + (screenY2 << 2);
			sC2 = (sLightlineData[0] << 8) + sLightlineData[1];
		   }
		  }
		 } else {
		  sLightlineData = firstLightlineRow - lightLineRowSize + (screenY1 << 2);
		  sC1 = (sLightlineData[0] << 8) + sLightlineData[1];
		  if (screenY2 < 0) {
		   sC2 = sCmin + (sCsl * (screenY2 - y0)) / (yMax - 1 - y0);
		  } else {
		   sLightlineData = firstLightlineRow - lightLineRowSize + (screenY2 << 2);
		   sC2 = (sLightlineData[0] << 8) + sLightlineData[1];
		  }
		 }
		 if (!sC1) {sC1 = 1;}
		 if (!sC2) {sC2 = 1;}

		 // for each column
		 for (sXt = sXmin; sXt < sXmax; sXt+=step) {
		  if (sInverse) {
		   sH0 = (screenX2 - 1 - sXt) * sSlope >> 7;
		   dx1 = (screenX2 - 1 - sXt << 12) / sC1;
		   dx2 = (sXt - screenX1 << 12) / sC2;
		  } else {
		   sH0 = (sXt - screenX1) * sSlope >> 7;
		   dx1 = (sXt - screenX1 << 12) / sC1;
		   dx2 = (screenX2 - 1 - sXt << 12) / sC2;
		  } // end if sInverse
		  sH0 = sH0 - (screenY1 - yt);
		  // calculate column x-coordinate
		  if (rYt - sH0 < yMin) {continue;} // out of the screen : top
		  
		  if (!(dx1 + dx2)) {continue;}
		  if (sInverse) {
		   sX = sDispOffset + (sDispWidth * dx2) / (dx1 + dx2) << 2;
		  } else {
		   sX = sDispOffset + (sDispWidth * dx1) / (dx1 + dx2) << 2;
		  }
		  if (sX < 0 || sX >= sRowSize) {continue;}
		  // get zoom factor
		  if (sH0 < 0) { // yt < screenY1 - out of the screen : bottom
		   sLux_b = 0;
		   sLux_g = 0;
		   sLux_r = 0;
		   sLux_d = 0;
		   if (type) {
			sFYt = sCminHT2 + (sCslHT2 * (yt - sH0 - y0)) / (yMax - 1 - y0);
		   } else {
			sFYt = sCminHT0 + (sCslHT0 * (yt - sH0 - y0)) / (yMax - 1 - y0);
		   }
		   sFYth = sFYt;
		   sVFading = 0;
		   sHbase = 0;
		  } else {
		   sLightlineData = firstLightlineRow + (yt - sH0 << 2);
		   sLux_b = sLightlineData[0];
		   sLux_g = sLightlineData[1];
		   sLux_r = sLightlineData[2];
		   sLux_d = sLightlineData[3];
		   sLightlineData -= lightLineRowSize;
		   sFYt = (sLightlineData[type] << 8) + sLightlineData[type + 1];
		   sFYth = (sLightlineData[0] << 8) + sLightlineData[1];
		   sVFading = sLightlineData[2];
		   sHbase = sH0;
		  }
		  sH0 += (sDh * sFYth >> 15);
		  sHend = sH0;
		  if (sH0 < 0) {
		   sHend = 0;
		  } /*else {
		   sHend = sH0;
		  }*/
		  if (rYt - sH0 < yMin) {continue;} // out of the screen : top
		  //sRealHeight = (sHeight * screenWidth >> 7) * sFYt >> 15;
		  sRealHeight = sHeight * sFYt >> 12;
		  if (sRealHeight < 2) {continue;}
		  
		  /*sHinit = sRealHeight + sH0;
		  if (rYt - sRealHeight - sH0 < yMin) {
		   sHinit = rYt - yMin;
		  }*/
		  if (rYt - sRealHeight - sH0 < yMin) {
		   sHinit = rYt - yMin;
		  } else {
		   sHinit = sRealHeight + sH0;
		  }
		  sFh = ((sHeight - 1) << 10) / (sRealHeight - 1);

		  /*sHMax = ysize + oScrY;
		  if (yt < yMax - 1) {
		   sLightlineData = firstLightlineRow - (lightLineRowSize << 1) + (sXt << 2);
		   sHMax = (sLightlineData[0] << 8) + sLightlineData[1];
		  }*/
		  if (yt == yMax - 1) {
		   sHMax = ysize + oScrY;
		  } else {
		   sLightlineData = firstLightlineRow - (lightLineRowSize << 1) + (sXt << 2);
		   sHMax = (sLightlineData[0] << 8) + sLightlineData[1];
		  }

		  // draw column
		  for (h = sHinit; h-->sHend;) {
		   if (rYt - h > sHMax) {break;}
		   if (rYt - h > yMaxDraw - 1) {break;}
		   sData = firstSRow - (sHeight - 1 - ((h - sH0) * sFh >> 10)) * sRowSize + sX;
		   if (sData[3]) {
			sScreenData = firstSScreenRow - (rYt - h) * sScreenRowSize + (sXt << 3);
			if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 && sScreenData[2] + sScreenData[3] + 2 >= rYt - sHend) {
			 // opaque surface
			 continue;
			}
			blue = sData[0];
			green = sData[1];
			red = sData[2];
			alpha = sData[3];
			if (sLux_d) {
			 blue += sLux_b;
			 green += sLux_g;
			 red += sLux_r;
			 if (blue > 255) {blue = 255;}
			 if (green > 255) {green = 255;}
			 if (red > 255) {red = 255;}
		   } else { // sLux_d == 0
			 blue -= sLux_b;
			 green -= sLux_g;
			 red -= sLux_r;
			} // end if sLux_d
			if (sScreenData[0] && (sBlend || sData[3] < 255 || sScreenData[2] + sScreenData[3] >= rYt - sHend)) {
			 // surface translucide
			 blend = sScreenData[1];
			 sOpacity = sScreenData[7];
			 if (!blend) {
			  blue = blue * (255 - sOpacity) + sScreenData[4] * sOpacity >> 8;
			  green = green * (255 - sOpacity) + sScreenData[5] * sOpacity >> 8;
			  red = red * (255 - sOpacity) + sScreenData[6] * sOpacity >> 8;
			 } else if (blend == 1) {
			  blue = blue + (sScreenData[4] * sOpacity >> 8);
			  green = green + (sScreenData[5] * sOpacity >> 8);
			  red = red + (sScreenData[6] * sOpacity >> 8);
			  if (blue > 255) {blue = 255;}
			  if (green > 255) {green = 255;}
			  if (red > 255) {red = 255;}
			 } else if (blend == 2) {
			  blue = blue - (sScreenData[4] * sOpacity >> 8);
			  green = green - (sScreenData[5] * sOpacity >> 8);
			  red = red - (sScreenData[6] * sOpacity >> 8);
			 }
			 alpha = ~((char) (((255 - alpha) * (255 - sScreenData[7])) / 255));
			} // end if surface translucide
			if (blue < 0) {blue = 0;}
			if (green < 0) {green = 0;}
			if (red < 0) {red = 0;}
			sScreenData[0] = 1;
			sScreenData[1] = sBlend;
			if (rYt - sHbase > 510)  {
			 sScreenData[2] = 255;
			 sScreenData[3] = 255;
			} else if (rYt - sHbase > 255)  {
			 sScreenData[2] = rYt - sHbase - 255;
			 sScreenData[3] = 255;
			} else {
			 sScreenData[2] = 0;
			 sScreenData[3] = rYt - sHbase;
			}
			sScreenData[4] = blue;
			sScreenData[5] = green;
			sScreenData[6] = red;
			sScreenData[7] = alpha;
		   } // end if sData[3]
		  } // end for h
		 } // end for sXt
	 } // end if dx
	} // end if xt < screenX2
	// prepare the next surface
	if (sCount < sNumber) {
	 rArraySurface = (RArray*)(*(ptr + sCount));
     sPtr = rArraySurface->ptr;
     type = *sPtr >> 1;
     screenX1 = *(sPtr + 1) >> 1;
     screenY1 = *(sPtr + 2) >> 1;
	 screenX2 = *(sPtr + 3) >> 1;
     screenY2 = *(sPtr + 4) >> 1;
	 sInverse = *(sPtr + 5) >> 1;
     sBitmap = ((RGSSBITMAP*) *(sPtr + 6))->bm->bminfo;
	 sDh = *(sPtr + 7) >> 1;
	 sBlend = *(sPtr + 8) >> 1;
	 sDispWidth = *(sPtr + 9) >> 1;
     sDispOffset = *(sPtr + 10) >> 1;
     firstSRow = (LPBYTE) sBitmap->firstRow;
     sWidth = sBitmap->infoheader->biWidth;
     sHeight = sBitmap->infoheader->biHeight;
     sRowSize = sWidth << 2;
	 sCount++;
	} else {
	 sNext = 0;
    } // end if sCount < sNumber
   } // end if sNext && yt == screenY1 && xt >= screenX1
   // end surfaces

   if (ym <= ody) {continue;}
   ground = 0;
   for (yd = dy; rYt - yd < ym; yd--) {
    if (rYt - yd + 1 - yMaxDraw > 0) {break;}
    screenData = firstScreenRow - (rYt - yd) * screenRowSize + (xt << 2);
	sScreenData = firstSScreenRow - (rYt - yd) * sScreenRowSize + (xt << 3);
	if (sScreenData[0] && !sScreenData[1] && sScreenData[7] == 255 && sScreenData[2] + sScreenData[3] >= rYt) {
	 screenData[0] = sScreenData[4];
	 screenData[1] = sScreenData[5];
	 screenData[2] = sScreenData[6];
	 screenData[3] = 255;
	 *sScreenData = 0;
	 continue;
	}
	if (yd < dy && yt + 1 - yMax == 0 && !noBlack) {
     screenData[0] = 0;
     screenData[1] = 0;
     screenData[2] = 0;
     screenData[3] = 255;
	 *sScreenData = 0;
	} else { // !(yd < dy && yt + 1 - screenHeight == 0)
	 if (dy - yd) { // dy > yd
	  totHA = 0;
	  ground = 1;
      for (itLayer = nbLayers; itLayer-->0;) {
	   if (dy - yd <= dA[itLayer]) {
	    if (!initA[itLayer]) {
	     tileIndex = *(ptrTileIndex + itLayer + 1) << 5;
		 colormapData = colormapBegin - ((tileIndex + ysr) * 10 << 6);
		 oColor = colormapData[xsr << 2];
		 if (oColor == 32) {
		  colormapData = colormapBegin - ((tileIndex + xsr) * 10 << 6) + (oColor << 2);
		 } else if (oColor == 64) {
		  colormapData = colormapData + (oColor << 2);
		 } else if (oColor == 96) {
		  colormapData = colormapBegin - ((tileIndex + xsr) * 10 << 6) + (oColor << 2);
		 } else {
		  colormapData = colormapData + (oColor << 2);
		 }
	     initA[itLayer] = 1;
	    } // end if !initA[itLayer]
	    pos = 31 - lA[itLayer] + ((dy + odyh - yd - totHA) * lA[itLayer]) / hA[itLayer] << 2;
		ground = 0;
		break;
	   } // end if dy - yd <= dA[itLayer]
	   totHA = dA[itLayer];
      } // end for itLayer
	  if (!ground && colormapData[pos + 3]) {
	   blue = colormapData[pos];
	   green = colormapData[pos + 1];
	   red = colormapData[pos + 2];
	  } else {
	   blue = mapTilesetData[0];
	   green = mapTilesetData[1];
	   red = mapTilesetData[2];
	  } // end if !ground && colormapData[pos + 3]
	  top = 0;
	 } else { // dy == yd
	  top = 1;
	  blue = mapTilesetData[0];
	  green = mapTilesetData[1];
	  red = mapTilesetData[2];
	 } // end if dy - yd
	 if (lux_d) {
	  blue += lux_b;
	  green += lux_g;
	  red += lux_r;
	  if (shadow && (top || ground)) {
	   blue += oShadow;
	   green += oShadow;
	   red += oShadow;
	   if (blue < 0) {blue = 0;} else if (blue > 255) {blue = 255;}
	   if (green < 0) {green = 0;} else if (green > 255) {green = 255;}
	   if (red < 0) {red = 0;} else if (red > 255) {red = 255;}
	  } else { // shadow == 0
	   if (blue > 255) {blue = 255;}
	   if (green > 255) {green = 255;}
	   if (red > 255) {red = 255;}
	  } // end if shadow
	 } else { // lux_d == 0
	  blue -= lux_b;
	  green -= lux_g;
	  red -= lux_r;
	  if (shadow && (top || ground)) {
	   blue += oShadow;
	   green += oShadow;
	   red += oShadow;
	  } // end if shadow
	  if (blue < 0) {blue = 0;} else if (blue > 255) {blue = 255;}
	  if (green < 0) {green = 0;} else if (green > 255) {green = 255;}
	  if (red < 0) {red = 0;} else if (red > 255) {red = 255;}
	 } // end if lux_d
	 if (sScreenData[0] && sScreenData[2] + sScreenData[3] >= rYt) {
	  blend = sScreenData[1];
	  sOpacity = sScreenData[7];
	  if (blend == 0) {
       blue = blue * (255 - sOpacity) + sScreenData[4] * sOpacity >> 8;
       green = green * (255 - sOpacity) + sScreenData[5] * sOpacity >> 8;
       red = red * (255 - sOpacity) + sScreenData[6] * sOpacity >> 8;
	  } else if (blend == 1) {
	   blue = blue + (sScreenData[4] * sOpacity >> 8);
	   green = green + (sScreenData[5] * sOpacity >> 8);
	   red = red + (sScreenData[6] * sOpacity >> 8);
	   if (blue > 255) {blue = 255;}
	   if (green > 255) {green = 255;}
	   if (red > 255) {red = 255;}
	  } else if (blend == 2) {
	   blue = blue - (sScreenData[4] * sOpacity >> 8);
	   green = green - (sScreenData[5] * sOpacity >> 8);
	   red = red - (sScreenData[6] * sOpacity >> 8);
	   if (blue < 0) {blue = 0;}
	   if (green < 0) {green = 0;}
	   if (red < 0) {red = 0;}
	  }
	 }
	 screenData[0] = blue;
     screenData[1] = green;
     screenData[2] = red;
     screenData[3] = alpha;
	 *sScreenData = 0;
	} // end if yd < dy && yt + 1 - screenHeight == 0
   } // end for yd
   lightlineData[0] = ody >> 8;
   lightlineData[1] = ody - (lightlineData[0] << 8);
   if (rYt < yMaxDraw) {
    *(firstSScreenRow - rYt * sScreenRowSize + (xt << 3)) = 0;
   }
  } // end for xt
 } // end for yt
 for (xt = xMin + x0; xt < xMax; xt += step) { // for each column
  lightlineData = firstLightlineRow - (lightLineRowSize << 1) + (xt << 2);
  y0min = (lightlineData[0] << 8) + lightlineData[1];
  for (yt = y0min; yt-->yMin;) { // for each row
   screenData = firstScreenRow - yt * screenRowSize + (xt << 2);
   sScreenData = firstSScreenRow - yt * sScreenRowSize + (xt << 3);
   if (*sScreenData) {
    // surface
    if (!sScreenData[1] && sScreenData[7] == 255) {
     // surface opaque
     blue = sScreenData[4];
     green = sScreenData[5];
     red = sScreenData[6];
    } else {
     // surface translucide ou avec un mode d'opacité
     blend = sScreenData[1];
     if (blend == 2) {
      // mode obscurci
      blue = 0;
      green = 0;
      red = 0;
     } else { // blend = 1
      // mode éclairci
      sOpacity = sScreenData[7];
      blue = sScreenData[4] * sOpacity >> 8;
      green = sScreenData[5] * sOpacity >> 8;
      red = sScreenData[6] * sOpacity >> 8;
     } // end if blend
    } // end if surface opaque/translucide
    *screenData++ = blue;
    *screenData++ = green;
    *screenData++ = red;
    *screenData = sScreenData[7];
    *sScreenData = 0;
    continue;
   } // end if surface
   *(screenData + 3) = 0;
  } // end for yt
 } // end for xt
 free(initA);
 free(lA);
 free(hA);
 free(dA);
 return oCamera;
}

/***************************************************************
* APPLY OPACITY
***************************************************************/

int EXPORT applyOpacity(const int bmpId, const int opacity) {

 // variables declaration
 RGSSBMINFO *bmp;
 LPBYTE bmpBegin, bmpData;
 int i, j, wBmp, hBmp, rsBmp, op;

 // variables initialization
 bmp = ((RGSSBITMAP*) (bmpId << 1))->bm->bminfo;
 bmpBegin = (LPBYTE) bmp->firstRow;
 wBmp = bmp->infoheader->biWidth;
 hBmp = bmp->infoheader->biHeight;
 rsBmp = wBmp << 2;
 op = opacity + 1;

 // drawing
 for (i = hBmp; i-->0;) {
  for (j = wBmp; j-->0;) {
   bmpData = bmpBegin - (i * rsBmp) + (j << 2);
   bmpData[3] = bmpData[3] * op >> 8;
  } // end for j
 } // end for i
 return 0;
}

/***************************************************************
* APPLY ZOOM
***************************************************************/

int EXPORT applyZoom(const int bmpId, const int srcId, const int lissage) {

 // variables declaration
 RGSSBMINFO *bmp, *src;
 LPBYTE bBmp, dBmp, bSrc, dSrc, dcBmp, dcSrc1, dcSrc2, dcSrc3, dcSrc4;
 int i, j, is, js, ise, jse, isr, jsr, isrc, jsrc, wBmp, hBmp, rsBmp, wSrc, hSrc, rsSrc;
 char c1, c2, c3, c4, nbc;

 // variables initialization
 bmp = ((RGSSBITMAP*) (bmpId << 1))->bm->bminfo;
 bBmp = (LPBYTE) bmp->firstRow;
 wBmp = bmp->infoheader->biWidth;
 hBmp = bmp->infoheader->biHeight;
 rsBmp = wBmp << 2;
 src = ((RGSSBITMAP*) (srcId << 1))->bm->bminfo;
 bSrc = (LPBYTE) src->firstRow;
 wSrc = src->infoheader->biWidth;
 hSrc = src->infoheader->biHeight;
 rsSrc = wSrc << 2;

 // drawing
 for (i = hBmp; i-->0;) {
  is = (i * hSrc << 3) / hBmp;
  ise = is >> 3;
  isr = is - (ise << 3);
  isrc = 8 - isr;
  dSrc = bSrc - (ise * rsSrc);
  dBmp = bBmp - (i * rsBmp);
  for (j = wBmp; j-->0;) {
   nbc = 0;
   js = (j * wSrc << 3) / wBmp;
   jse = js >> 3;
   jsr = js - (jse << 3);
   jsrc = 8 - jsr;
   dcSrc1 = dSrc + (jse << 2);
   dcBmp = dBmp + (j << 2);
   if (lissage) {
    if (wBmp - 1 - j) {
	 dcSrc2 = dcSrc1 + 4;
	 if (hBmp - 1 - i) {
	  dcSrc3 = dcSrc1 - rsSrc;
	 } else {
	  dcSrc3 = dcSrc1;
	 }
	 dcSrc4 = dcSrc3 + 4;
	} else {
	 dcSrc2 = dcSrc1;
	 if (hBmp - 1 - i) {
	  dcSrc3 = dcSrc1 - rsSrc;
	 } else {
	  dcSrc3 = dcSrc1;
	 }
	 dcSrc4 = dcSrc3;
	}
	if (dcSrc1[3]) {
	 c1 = isrc * jsrc;
	 nbc++;
	} else {
	 c1 = 0;
	}
	if (dcSrc2[3]) {
	 c2 = isrc * jsr;
	 nbc++;
	} else {
	 c2 = 0;
	}
	if (dcSrc3[3]) {
	 c3 = isr * jsrc;
	 nbc++;
	} else {
	 c3 = 0;
	}
	if (dcSrc4[3]) {
	 c4 = isr * jsr;
	 nbc++;
	} else {
	 c4 = 0;
	}
	if (nbc) {
	 *dcBmp++ = c1 * (*dcSrc1++) + c2 * (*dcSrc2++) + c3 * (*dcSrc3++) + c4 * (*dcSrc4++) >> 6;
	 *dcBmp++ = c1 * (*dcSrc1++) + c2 * (*dcSrc2++) + c3 * (*dcSrc3++) + c4 * (*dcSrc4++) >> 6;
	 *dcBmp++ = c1 * (*dcSrc1++) + c2 * (*dcSrc2++) + c3 * (*dcSrc3++) + c4 * (*dcSrc4++) >> 6;
	 *dcBmp++ = c1 * (*dcSrc1++) + c2 * (*dcSrc2++) + c3 * (*dcSrc3++) + c4 * (*dcSrc4++) >> 6;
	} else { // USEFUL ONLY IF REAL-TIME
	 dcBmp[3] = 0;
	}
   } else {
    *dcBmp++ = *dcSrc1++;
    *dcBmp++ = *dcSrc1++;
    *dcBmp++ = *dcSrc1++;
    *dcBmp++ = *dcSrc1++;
   }
  } // end for j
 } // end for i
 return 0;
}