/*
Copyright (c) 2010 Peter "Corsix" Cawley

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "th_lua_internal.h"
#include "th_map.h"
#include "th_pathfind.h"
#include <string.h>

static int l_map_new(lua_State *L)
{
    luaT_stdnew<THMap>(L, luaT_environindex, true);
    return 1;
}

static int l_map_set_sheet(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    THSpriteSheet* pSheet = luaT_testuserdata<THSpriteSheet>(L, 2);
    lua_settop(L, 2);

    pMap->setBlockSheet(pSheet);
    luaT_setenvfield(L, 1, "sprites");
    return 1;
}

static int l_map_persist(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_settop(L, 2);
    lua_insert(L, 1);
    pMap->persist((LuaPersistWriter*)lua_touserdata(L, 1));
    return 0;
}

static int l_map_depersist(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_settop(L, 2);
    lua_insert(L, 1);
    LuaPersistReader* pReader = (LuaPersistReader*)lua_touserdata(L, 1);

    pMap->depersist(pReader);
    luaT_getenvfield(L, 2, "sprites");
    pMap->setBlockSheet((THSpriteSheet*)lua_touserdata(L, -1));
    lua_pop(L, 1);
    return 0;
}

static void l_map_load_obj_cb(void *pL, int iX, int iY, THObjectType eTHOB, uint8_t iFlags)
{
    lua_State *L = reinterpret_cast<lua_State*>(pL);
    lua_createtable(L, 4, 0);

    lua_pushinteger(L, 1 + (lua_Integer)iX);
    lua_rawseti(L, -2, 1);
    lua_pushinteger(L, 1 + (lua_Integer)iY);
    lua_rawseti(L, -2, 2);
    lua_pushinteger(L, (lua_Integer)eTHOB);
    lua_rawseti(L, -2, 3);
    lua_pushinteger(L, (lua_Integer)iFlags);
    lua_rawseti(L, -2, 4);

    lua_rawseti(L, 3, static_cast<int>(lua_objlen(L, 3)) + 1);
}

static int l_map_load(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    size_t iDataLen;
    const unsigned char* pData = luaT_checkfile(L, 2, &iDataLen);
    lua_settop(L, 2);
    lua_newtable(L);
    if(pMap->loadFromTHFile(pData, iDataLen, l_map_load_obj_cb, (void*)L))
        lua_pushboolean(L, 1);
    else
        lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;
}

static int l_map_loadblank(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    if(pMap->loadBlank())
        lua_pushboolean(L, 1);
    else
        lua_pushboolean(L, 0);
    lua_newtable(L);
    return 2;
}

THAnimation* l_map_updateblueprint_getnextanim(lua_State *L, int& iIndex)
{
    THAnimation *pAnim;
    lua_rawgeti(L, 10, iIndex);
    if(lua_type(L, -1) == LUA_TNIL)
    {
        lua_pop(L, 1);
        pAnim = luaT_new(L, THAnimation);
        lua_pushvalue(L, luaT_upvalueindex(2));
        lua_setmetatable(L, -2);
        lua_createtable(L, 0, 2);
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "map");
        lua_pushvalue(L, 11);
        lua_setfield(L, -2, "animator");
        lua_setfenv(L, -2);
        lua_rawseti(L, 10, iIndex);
    }
    else
    {
        pAnim = luaT_testuserdata<THAnimation>(L, -1, luaT_upvalueindex(2));
        lua_pop(L, 1);
    }
    ++iIndex;
    return pAnim;
}

static uint16_t l_check_temp(lua_State *L, int iArg)
{
    lua_Number n = luaL_checknumber(L, iArg);
    if(n < static_cast<lua_Number>(0) || static_cast<lua_Number>(1) < n)
        luaL_argerror(L, iArg, "temperature (number in [0,1])");
    return static_cast<uint16_t>(n * static_cast<lua_Number>(65535));
}

static int l_map_updatetemperature(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    uint16_t iAir = l_check_temp(L, 2);
    uint16_t iRadiator = l_check_temp(L, 3);
    pMap->updateTemperatures(iAir, iRadiator);
    lua_settop(L, 1);
    return 1;
}

static int l_map_gettemperature(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX = luaL_checkint(L, 2) - 1;
    int iY = luaL_checkint(L, 3) - 1;
    const THMapNode* pNode = pMap->getNode(iX, iY);
    uint16_t iTemp = pMap->getNodeTemperature(pNode);
    lua_pushnumber(L, static_cast<lua_Number>(iTemp) / static_cast<lua_Number>(65535));
    return 1;
}

static int l_map_updateblueprint(lua_State *L)
{
    // NB: This function can be implemented in Lua, but is implemented in C for
    // efficiency.
    const unsigned short iFloorTileGood = 24 + (THDF_Alpha50 << 8);
    const unsigned short iFloorTileGoodCenter = 37 + (THDF_Alpha50 << 8);
    const unsigned short iFloorTileBad  = 67 + (THDF_Alpha50 << 8);
    const unsigned int iWallAnimTopCorner = 124;
    const unsigned int iWallAnim = 120;

    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iOldX = luaL_checkint(L, 2) - 1;
    int iOldY = luaL_checkint(L, 3) - 1;
    int iOldW = luaL_checkint(L, 4);
    int iOldH = luaL_checkint(L, 5);
    int iNewX = luaL_checkint(L, 6) - 1;
    int iNewY = luaL_checkint(L, 7) - 1;
    int iNewW = luaL_checkint(L, 8);
    int iNewH = luaL_checkint(L, 9);
    luaL_checktype(L, 10, LUA_TTABLE); // Animation list
    THAnimationManager* pAnims = luaT_testuserdata<THAnimationManager>(L, 11, luaT_upvalueindex(1));
    bool entire_invalid = lua_toboolean(L, 12) != 0;
    bool valid = !entire_invalid;

    if(iOldX < 0 || iOldY < 0 || (iOldX + iOldW) > pMap->getWidth() || (iOldY + iOldH) > pMap->getHeight())
        luaL_argerror(L, 2, "Old rectangle is out of bounds");
    if(iNewX < 0 || iNewY < 0 || (iNewX + iNewW) >= pMap->getWidth() || (iNewY + iNewH) >= pMap->getHeight())
        luaL_argerror(L, 6, "New rectangle is out of bounds");

    // Clear old floor tiles
    for(int iY = iOldY; iY < iOldY + iOldH; ++iY)
    {
        for(int iX = iOldX; iX < iOldX + iOldW; ++iX)
        {
            THMapNode *pNode = pMap->getNodeUnchecked(iX, iY);
            pNode->iBlock[3] = 0;
            pNode->iFlags |= (pNode->iFlags & THMN_PassableIfNotForBlueprint) >> THMN_PassableIfNotForBlueprint_ShiftDelta;
            pNode->iFlags &= ~THMN_PassableIfNotForBlueprint;
        }
    }

#define IsValid(node) \
    (!entire_invalid && (((node)->iFlags & (THMN_Buildable | THMN_Room)) == THMN_Buildable))

    // Set new floor tiles
    for(int iY = iNewY; iY < iNewY + iNewH; ++iY)
    {
        for(int iX = iNewX; iX < iNewX + iNewW; ++iX)
        {
            THMapNode *pNode = pMap->getNodeUnchecked(iX, iY);
            if(IsValid(pNode))
                pNode->iBlock[3] = iFloorTileGood;
            else
            {
                pNode->iBlock[3] = iFloorTileBad;
                valid = false;
            }
            pNode->iFlags |= (pNode->iFlags & THMN_Passable) << THMN_PassableIfNotForBlueprint_ShiftDelta;
        }
    }

    // Set center floor tiles
    if(iNewW >= 2 && iNewH >= 2)
    {
        int iCenterX = iNewX + (iNewW - 2) / 2;
        int iCenterY = iNewY + (iNewH - 2) / 2;

        THMapNode *pNode = pMap->getNodeUnchecked(iCenterX, iCenterY);
        if(pNode->iBlock[3] == iFloorTileGood)
            pNode->iBlock[3] = iFloorTileGoodCenter + 2;
        pNode = pMap->getNodeUnchecked(iCenterX + 1, iCenterY);
        if(pNode->iBlock[3] == iFloorTileGood)
            pNode->iBlock[3] = iFloorTileGoodCenter + 1;
        pNode = pMap->getNodeUnchecked(iCenterX, iCenterY + 1);
        if(pNode->iBlock[3] == iFloorTileGood)
            pNode->iBlock[3] = iFloorTileGoodCenter + 0;
        pNode = pMap->getNodeUnchecked(iCenterX + 1, iCenterY + 1);
        if(pNode->iBlock[3] == iFloorTileGood)
            pNode->iBlock[3] = iFloorTileGoodCenter + 3;
    }

    // Set wall animations
    int iNextAnim = 1;
    THAnimation *pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
    THMapNode *pNode = pMap->getNodeUnchecked(iNewX, iNewY);
    pAnim->setAnimation(pAnims, iWallAnimTopCorner);
    pAnim->setFlags(THDF_ListBottom | (IsValid(pNode) ? 0 : THDF_AltPalette));
    pAnim->attachToTile(pNode);

    for(int iX = iNewX; iX < iNewX + iNewW; ++iX)
    {
        if(iX != iNewX)
        {
            pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
            pNode = pMap->getNodeUnchecked(iX, iNewY);
            pAnim->setAnimation(pAnims, iWallAnim);
            pAnim->setFlags(THDF_ListBottom | (IsValid(pNode) ? 0 : THDF_AltPalette));
            pAnim->attachToTile(pNode);
            pAnim->setPosition(0, 0);
        }
        pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
        pNode = pMap->getNodeUnchecked(iX, iNewY + iNewH - 1);
        pAnim->setAnimation(pAnims, iWallAnim);
        pAnim->setFlags(THDF_ListBottom | (IsValid(pNode) ? 0 : THDF_AltPalette));
        pNode = pMap->getNodeUnchecked(iX, iNewY + iNewH);
        pAnim->attachToTile(pNode);
        pAnim->setPosition(0, -1);
    }
    for(int iY = iNewY; iY < iNewY + iNewH; ++iY)
    {
        if(iY != iNewY)
        {
            pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
            pNode = pMap->getNodeUnchecked(iNewX, iY);
            pAnim->setAnimation(pAnims, iWallAnim);
            pAnim->setFlags(THDF_ListBottom | THDF_FlipHorizontal | (IsValid(pNode) ? 0 : THDF_AltPalette));
            pAnim->attachToTile(pNode);
            pAnim->setPosition(2, 0);
        }
        pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
        pNode = pMap->getNodeUnchecked(iNewX + iNewW - 1, iY);
        pAnim->setAnimation(pAnims, iWallAnim);
        pAnim->setFlags(THDF_ListBottom | THDF_FlipHorizontal | (IsValid(pNode) ? 0 : THDF_AltPalette));
        pNode = pMap->getNodeUnchecked(iNewX + iNewW, iY);
        pAnim->attachToTile(pNode);
        pAnim->setPosition(2, -1);
    }

#undef IsValid

    // Clear away extra animations
    int iAnimCount = (int)lua_objlen(L, 10);
    if(iAnimCount >= iNextAnim)
    {
        for(int i = iNextAnim; i <= iAnimCount; ++i)
        {
            pAnim = l_map_updateblueprint_getnextanim(L, iNextAnim);
            pAnim->removeFromTile();
            lua_pushnil(L);
            lua_rawseti(L, 10, i);
        }
    }

    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

static int l_map_getsize(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_pushinteger(L, pMap->getWidth());
    lua_pushinteger(L, pMap->getHeight());
    return 2;
}

static int l_map_get_player_count(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_pushinteger(L, pMap->getPlayerCount());
    return 1;
}

static int l_map_get_player_camera(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX, iY;
    int iPlayer = luaL_optint(L, 2, 1);
    bool bGood = pMap->getPlayerCameraTile(iPlayer - 1, &iX, &iY);
    if(!bGood)
        return luaL_error(L, "Player index out of range: %d", iPlayer);
    lua_pushinteger(L, iX + 1);
    lua_pushinteger(L, iY + 1);
    return 2;
}

static int l_map_get_player_heliport(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX, iY;
    int iPlayer = luaL_optint(L, 2, 1);
    bool bGood = pMap->getPlayerHeliportTile(iPlayer - 1, &iX, &iY);
    bGood = pMap->getPlayerHeliportTile(iPlayer - 1, &iX, &iY);
    if(!bGood)
        return luaL_error(L, "Player index out of range: %i", iPlayer);
    lua_pushinteger(L, iX + 1);
    lua_pushinteger(L, iY + 2);
    return 2;
}

static int l_map_getcell(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX = luaL_checkint(L, 2) - 1; // Lua arrays start at 1 - pretend
    int iY = luaL_checkint(L, 3) - 1; // the map does too.
    THMapNode* pNode = pMap->getNode(iX, iY);
    if(pNode == NULL)
    {
        return luaL_argerror(L, 2, lua_pushfstring(L, "Map co-ordinates out "
        "of bounds (%d, %d)", iX + 1, iY + 1));
    }
    if(lua_isnoneornil(L, 4))
    {
        lua_pushinteger(L, pNode->iBlock[0]);
        lua_pushinteger(L, pNode->iBlock[1]);
        lua_pushinteger(L, pNode->iBlock[2]);
        lua_pushinteger(L, pNode->iBlock[3]);
        return 4;
    }
    else
    {
        int iLayer = luaL_checkint(L, 4) - 1;
        if(iLayer < 0 || iLayer >= 4)
            return luaL_argerror(L, 4, "Layer index is out of bounds (1-4)");
        lua_pushinteger(L, pNode->iBlock[iLayer]);
        return 1;
    }
}

static int l_map_getcellflags(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX = luaL_checkint(L, 2) - 1; // Lua arrays start at 1 - pretend
    int iY = luaL_checkint(L, 3) - 1; // the map does too.
    THMapNode* pNode = pMap->getNode(iX, iY);
    if(pNode == NULL)
        return luaL_argerror(L, 2, "Map co-ordinates out of bounds");
    if(lua_type(L, 4) != LUA_TTABLE)
    {
        lua_settop(L, 3);
        lua_createtable(L, 0, 1);
    }
    else
    {
        lua_settop(L, 4);
    }

#define Flag(CName, LName) \
    { \
        lua_pushliteral(L, LName); \
        lua_pushboolean(L, (pNode->iFlags & CName) ? 1 : 0); \
        lua_settable(L, 4); \
    }

#define FlagInt(CField, LName) \
    { \
        lua_pushliteral(L, LName); \
        lua_pushinteger(L, pNode->CField); \
        lua_settable(L, 4); \
    }

    Flag(THMN_Passable, "passable")
    Flag(THMN_Hospital, "hospital")
    Flag(THMN_Buildable, "buildable")
    Flag(THMN_Room, "room")
    Flag(THMN_DoorWest, "doorWest")
    Flag(THMN_DoorNorth, "doorNorth")
    Flag(THMN_TallWest, "tallWest")
    Flag(THMN_TallNorth, "tallNorth")
    Flag(THMN_CanTravelN, "travelNorth")
    Flag(THMN_CanTravelE, "travelEast")
    Flag(THMN_CanTravelS, "travelSouth")
    Flag(THMN_CanTravelW, "travelWest")
    Flag(THMN_DoNotIdle, "doNotIdle")

    FlagInt(iRoomId, "roomId")
    FlagInt(iParcelId, "parcelId");
    FlagInt(iFlags >> 24, "thob")

#undef FlagInt
#undef Flag

    return 1;
}

static int l_map_setcellflags(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX = luaL_checkint(L, 2) - 1; // Lua arrays start at 1 - pretend
    int iY = luaL_checkint(L, 3) - 1; // the map does too.
    THMapNode* pNode = pMap->getNode(iX, iY);
    if(pNode == NULL)
        return luaL_argerror(L, 2, "Map co-ordinates out of bounds");
    luaL_checktype(L, 4, LUA_TTABLE);
    lua_settop(L, 4);

#define Flag(CName, LName) \
    if(strcmp(field, LName) == 0) \
    { \
        if(lua_toboolean(L, 6) == 0) \
            pNode->iFlags &= ~CName; \
        else \
            pNode->iFlags |= CName; \
    } else

    lua_pushnil(L);
    while(lua_next(L, 4))
    {
        if(lua_type(L, 5) == LUA_TSTRING)
        {
            const char *field = lua_tostring(L, 5);
            Flag(THMN_Passable, "passable")
            Flag(THMN_Hospital, "hospital")
            Flag(THMN_Buildable, "buildable")
            Flag(THMN_Room, "room")
            Flag(THMN_DoorWest, "doorWest")
            Flag(THMN_DoorNorth, "doorNorth")
            Flag(THMN_TallWest, "tallWest")
            Flag(THMN_TallNorth, "tallNorth")
            Flag(THMN_DoNotIdle, "doNotIdle")
            /* else */ if(strcmp(field, "thob") == 0)
            {
                pNode->iFlags &= 0x00FFFFFF;
                pNode->iFlags |= static_cast<uint32_t>(lua_tointeger(L, 6)) << 24;
            }
            else if(strcmp(field, "parcelId") == 0)
            {
                pNode->iParcelId = static_cast<uint16_t>(lua_tointeger(L, 6));
            }
            else
            {
                luaL_error(L, "Invalid flag \'%s\'", field);
            }
        }
        lua_settop(L, 5);
    }

#undef Flag

    return 0;
}

static int l_map_setwallflags(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    pMap->setAllWallDrawFlags((unsigned char)luaL_checkint(L, 2));
    lua_settop(L, 1);
    return 1;
}

static int l_map_setcell(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX = luaL_checkint(L, 2) - 1; // Lua arrays start at 1 - pretend
    int iY = luaL_checkint(L, 3) - 1; // the map does too.
    THMapNode* pNode = pMap->getNode(iX, iY);
    if(pNode == NULL)
        return luaL_argerror(L, 2, "Map co-ordinates out of bounds");
    if(lua_gettop(L) >= 7)
    {
        pNode->iBlock[0] = (uint16_t)luaL_checkint(L, 4);
        pNode->iBlock[1] = (uint16_t)luaL_checkint(L, 5);
        pNode->iBlock[2] = (uint16_t)luaL_checkint(L, 6);
        pNode->iBlock[3] = (uint16_t)luaL_checkint(L, 7);
    }
    else
    {
        int iLayer = luaL_checkint(L, 4) - 1;
        if(iLayer < 0 || iLayer >= 4)
            return luaL_argerror(L, 4, "Layer index is out of bounds (1-4)");
        int iBlock = luaL_checkint(L, 5);
        pNode->iBlock[iLayer] = (uint16_t)iBlock;
    }

    lua_settop(L, 1);
    return 1;
}

static int l_map_updateshadows(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    pMap->updateShadows();
    lua_settop(L, 1);
    return 1;
}

static int l_map_mark_room(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX_ = luaL_checkint(L, 2) - 1;
    int iY_ = luaL_checkint(L, 3) - 1;
    int iW = luaL_checkint(L, 4);
    int iH = luaL_checkint(L, 5);
    int iTile = luaL_checkint(L, 6);
    int iRoomId = luaL_optint(L, 7, 0);

    if(iX_ < 0 || iY_ < 0 || (iX_ + iW) > pMap->getWidth() || (iY_ + iH) > pMap->getHeight())
        luaL_argerror(L, 2, "Rectangle is out of bounds");

    for(int iY = iY_; iY < iY_ + iH; ++iY)
    {
        for(int iX = iX_; iX < iX_ + iW; ++iX)
        {
            THMapNode *pNode = pMap->getNodeUnchecked(iX, iY);
            pNode->iBlock[0] = iTile;
            pNode->iBlock[3] = 0;
            uint32_t iFlags = pNode->iFlags;
            iFlags |= THMN_Room;
            iFlags |= (iFlags & THMN_PassableIfNotForBlueprint) >> THMN_PassableIfNotForBlueprint_ShiftDelta;
            iFlags &= ~THMN_PassableIfNotForBlueprint;
            pNode->iFlags = iFlags;
            pNode->iRoomId = iRoomId;
        }
    }

    pMap->updatePathfinding();
    pMap->updateShadows();

    lua_settop(L, 1);
    return 1;
}

static int l_map_unmark_room(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iX_ = luaL_checkint(L, 2) - 1;
    int iY_ = luaL_checkint(L, 3) - 1;
    int iW = luaL_checkint(L, 4);
    int iH = luaL_checkint(L, 5);

    if(iX_ < 0 || iY_ < 0 || (iX_ + iW) > pMap->getWidth() || (iY_ + iH) > pMap->getHeight())
        luaL_argerror(L, 2, "Rectangle is out of bounds");

    for(int iY = iY_; iY < iY_ + iH; ++iY)
    {
        for(int iX = iX_; iX < iX_ + iW; ++iX)
        {
            THMapNode *pNode = pMap->getNodeUnchecked(iX, iY);
            pNode->iBlock[0] = pMap->getOriginalNodeUnchecked(iX, iY)->iBlock[0];
            pNode->iFlags &= ~THMN_Room;
            pNode->iRoomId = 0;
        }
    }

    pMap->updatePathfinding();
    pMap->updateShadows();

    lua_settop(L, 1);
    return 1;
}

static int l_map_draw(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    THRenderTarget* pCanvas = luaT_testuserdata<THRenderTarget>(L, 2);

    pMap->draw(pCanvas, luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5),
        luaL_checkint(L, 6), luaL_optint(L, 7, 0), luaL_optint(L, 8, 0));

    lua_settop(L, 1);
    return 1;
}

static int l_map_hittest(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    THDrawable* pObject = pMap->hitTest(luaL_checkint(L, 2), luaL_checkint(L, 3));
    if(pObject == NULL)
        return 0;
    lua_rawgeti(L, luaT_upvalueindex(1), 1);
    lua_pushlightuserdata(L, pObject);
    lua_gettable(L, -2);
    return 1;
}

static int l_map_get_parcel_tilecount(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    int iParcel = luaL_checkint(L, 2);
    int iCount = pMap->getParcelTileCount(iParcel);
    lua_pushinteger(L, iCount);
    return 1;
}

static int l_map_get_parcel_count(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_pushinteger(L, pMap->getParcelCount());
    return 1;
}

static int l_map_set_parcel_owner(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    pMap->setParcelOwner(luaL_checkint(L, 2), luaL_checkint(L, 3));
    lua_settop(L, 1);
    return 1;
}

static int l_map_get_parcel_owner(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_pushinteger(L, pMap->getParcelOwner(luaL_checkint(L, 2)));
    return 1;
}

static int l_map_is_parcel_purchasable(lua_State *L)
{
    THMap* pMap = luaT_testuserdata<THMap>(L);
    lua_pushboolean(L, pMap->isParcelPurchasable(luaL_checkint(L, 2),
        luaL_checkint(L, 3)) ? 1 : 0);
    return 1;
}

static int l_path_new(lua_State *L)
{
    luaT_stdnew<THPathfinder>(L, luaT_environindex, true);
    return 1;
}

static int l_path_set_map(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    THMap* pMap = luaT_testuserdata<THMap>(L, 2);
    lua_settop(L, 2);

    pPathfinder->setDefaultMap(pMap);
    luaT_setenvfield(L, 1, "map");
    return 1;
}

static int l_path_persist(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    lua_settop(L, 2);
    lua_insert(L, 1);
    pPathfinder->persist((LuaPersistWriter*)lua_touserdata(L, 1));
    return 0;
}

static int l_path_depersist(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    lua_settop(L, 2);
    lua_insert(L, 1);
    LuaPersistReader* pReader = (LuaPersistReader*)lua_touserdata(L, 1);

    pPathfinder->depersist(pReader);
    luaT_getenvfield(L, 2, "map");
    pPathfinder->setDefaultMap(reinterpret_cast<THMap*>(lua_touserdata(L, -1)));
    return 0;
}

static int l_path_is_reachable_from_hospital(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    if(pPathfinder->findPathToHospital(NULL, luaL_checkint(L, 2) - 1,
        luaL_checkint(L, 3) - 1))
    {
        lua_pushboolean(L, 1);
        int iX, iY;
        pPathfinder->getPathEnd(&iX, &iY);
        lua_pushinteger(L, iX + 1);
        lua_pushinteger(L, iY + 1);
        return 3;
    }
    else
    {
        lua_pushboolean(L, 0);
        return 1;
    }
}

static int l_path_distance(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    if(pPathfinder->findPath(NULL, luaL_checkint(L, 2) - 1, luaL_checkint(L, 3) - 1,
        luaL_checkint(L, 4) - 1, luaL_checkint(L, 5) - 1))
    {
        lua_pushinteger(L, pPathfinder->getPathLength());
    }
    else
    {
        lua_pushboolean(L, 0);
    }
    return 1;
}

static int l_path_path(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    pPathfinder->findPath(NULL, luaL_checkint(L, 2) - 1, luaL_checkint(L, 3) - 1,
        luaL_checkint(L, 4) - 1, luaL_checkint(L, 5) - 1);
    pPathfinder->pushResult(L);
    return 2;
}

static int l_path_idle(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    if(!pPathfinder->findIdleTile(NULL, luaL_checkint(L, 2) - 1,
        luaL_checkint(L, 3) - 1, luaL_optint(L, 4, 0)))
    {
        return 0;
    }
    int iX, iY;
    pPathfinder->getPathEnd(&iX, &iY);
    lua_pushinteger(L, iX + 1);
    lua_pushinteger(L, iY + 1);
    return 2;
}

static int l_path_visit(lua_State *L)
{
    THPathfinder* pPathfinder = luaT_testuserdata<THPathfinder>(L);
    luaL_checktype(L, 6, LUA_TFUNCTION);
    lua_pushboolean(L, pPathfinder->visitObjects(NULL, luaL_checkint(L, 2) - 1,
        luaL_checkint(L, 3) - 1, static_cast<THObjectType>(luaL_checkint(L, 4)),
        luaL_checkint(L, 5), L, 6, luaL_checkint(L, 4) == 0 ? true : false) ? 1 : 0);
    return 1;
}

void THLuaRegisterMap(const THLuaRegisterState_t *pState)
{
    // Map
    luaT_class(THMap, l_map_new, "map", MT_Map);
    luaT_setmetamethod(l_map_persist, "persist", MT_Anim);
    luaT_setmetamethod(l_map_depersist, "depersist", MT_Anim);
    luaT_setfunction(l_map_load, "load");
    luaT_setfunction(l_map_loadblank, "loadBlank");
    luaT_setfunction(l_map_getsize, "size");
    luaT_setfunction(l_map_get_player_count, "getPlayerCount");
    luaT_setfunction(l_map_get_player_camera, "getCameraTile");
    luaT_setfunction(l_map_get_player_heliport, "getHeliportTile");
    luaT_setfunction(l_map_getcell, "getCell");
    luaT_setfunction(l_map_gettemperature, "getCellTemperature");
    luaT_setfunction(l_map_getcellflags, "getCellFlags");
    luaT_setfunction(l_map_setcellflags, "setCellFlags");
    luaT_setfunction(l_map_setcell, "setCell");
    luaT_setfunction(l_map_setwallflags, "setWallDrawFlags");
    luaT_setfunction(l_map_updatetemperature, "updateTemperatures");
    luaT_setfunction(l_map_updateblueprint, "updateRoomBlueprint", MT_Anims, MT_Anim);
    luaT_setfunction(l_map_updateshadows, "updateShadows");
    luaT_setfunction(l_map_mark_room, "markRoom");
    luaT_setfunction(l_map_unmark_room, "unmarkRoom");
    luaT_setfunction(l_map_set_sheet, "setSheet", MT_Sheet);
    luaT_setfunction(l_map_draw, "draw", MT_Surface);
    luaT_setfunction(l_map_hittest, "hitTestObjects", MT_Anim);
    luaT_setfunction(l_map_get_parcel_tilecount, "getParcelTileCount");
    luaT_setfunction(l_map_get_parcel_count, "getPlotCount");
    luaT_setfunction(l_map_set_parcel_owner, "setPlotOwner");
    luaT_setfunction(l_map_get_parcel_owner, "getPlotOwner");
    luaT_setfunction(l_map_is_parcel_purchasable, "isParcelPurchasable");
    luaT_endclass();

    // Pathfinder
    luaT_class(THPathfinder, l_path_new, "pathfinder", MT_Path);
    luaT_setmetamethod(l_path_persist, "persist");
    luaT_setmetamethod(l_path_depersist, "depersist");
    luaT_setfunction(l_path_distance, "findDistance");
    luaT_setfunction(l_path_is_reachable_from_hospital, "isReachableFromHospital");
    luaT_setfunction(l_path_path, "findPath");
    luaT_setfunction(l_path_idle, "findIdleTile");
    luaT_setfunction(l_path_visit, "findObject");
    luaT_setfunction(l_path_set_map, "setMap", MT_Map);
    luaT_endclass();
}
