#include "gge.h"
#include "SDL_events.h"
#include "SDL_syswm.h"

static int LUA_GetEvent(lua_State* L)
{
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");

    switch (ev->type)
    {
    case SDL_DISPLAYEVENT: //display
        lua_createtable(L, 4, 8);
        lua_pushinteger(L, ev->display.type);
        lua_seti(L, -2, 1);
        lua_pushinteger(L, ev->display.event);
        lua_seti(L, -2, 2);
        lua_pushinteger(L, ev->display.display);
        lua_seti(L, -2, 3);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 4);

        lua_pushinteger(L, ev->display.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->display.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->display.display);
        lua_setfield(L, -2, "display");
        lua_pushinteger(L, ev->display.event);
        lua_setfield(L, -2, "event");
        lua_pushinteger(L, ev->display.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->display.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->display.padding3);
        lua_setfield(L, -2, "padding3");
        lua_pushinteger(L, ev->display.data1);
        lua_setfield(L, -2, "data1");
        break;
    case SDL_WINDOWEVENT: //window
        lua_createtable(L, 2, 9);
        //lua_pushinteger(L,ev->window.type);
        //lua_seti(L,-2,1);
        lua_pushinteger(L, ev->window.event);
        lua_seti(L, -2, 1);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 2);

        lua_pushinteger(L, ev->window.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->window.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->window.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushinteger(L, ev->window.event);
        lua_setfield(L, -2, "event");
        lua_pushinteger(L, ev->window.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->window.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->window.padding3);
        lua_setfield(L, -2, "padding3");
        lua_pushinteger(L, ev->window.data1);
        lua_setfield(L, -2, "data1");
        lua_pushinteger(L, ev->window.data2);
        lua_setfield(L, -2, "data2");
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: //key
        lua_createtable(L, 5, 8);
        lua_pushinteger(L, ev->key.keysym.sym);
        lua_seti(L, -2, 1);
        lua_pushinteger(L, ev->key.keysym.mod);
        lua_seti(L, -2, 2);
        lua_pushboolean(L, ev->key.state);
        lua_seti(L, -2, 3);
        lua_pushboolean(L, ev->key.repeat);
        lua_seti(L, -2, 4);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 5);

        lua_pushinteger(L, ev->key.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->key.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->key.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushboolean(L, ev->key.state);
        lua_setfield(L, -2, "state");
        lua_pushboolean(L, ev->key.repeat);
        lua_setfield(L, -2, "repeat");
        lua_pushinteger(L, ev->key.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->key.padding3);
        lua_setfield(L, -2, "padding3");

        lua_createtable(L, 0, 4);
        lua_pushinteger(L, ev->key.keysym.scancode);
        lua_setfield(L, -2, "scancode");
        lua_pushinteger(L, ev->key.keysym.sym);
        lua_setfield(L, -2, "sym");
        lua_pushinteger(L, ev->key.keysym.mod);
        lua_setfield(L, -2, "mod");
        lua_pushinteger(L, ev->key.keysym.unused);
        lua_setfield(L, -2, "unused");
        lua_setfield(L, -2, "keysym");
        break;
    case SDL_TEXTEDITING: //edit
        lua_createtable(L, 2, 6);
        lua_pushstring(L, ev->text.text);
        lua_seti(L, -2, 1);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 2);

        lua_pushinteger(L, ev->edit.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->edit.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->edit.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushstring(L, ev->edit.text);
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, ev->edit.start);
        lua_setfield(L, -2, "start");
        lua_pushinteger(L, ev->edit.length);
        lua_setfield(L, -2, "length");
        break;
    case SDL_TEXTINPUT: //text
        lua_createtable(L, 2, 4);
        lua_pushstring(L, ev->text.text);
        lua_seti(L, -2, 1);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 2);

        lua_pushinteger(L, ev->text.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->text.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->text.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushstring(L, ev->text.text);
        lua_setfield(L, -2, "text");
        break;
    case SDL_MOUSEMOTION: //motion
        lua_createtable(L, 5, 9);
        lua_pushinteger(L, ev->motion.type);
        lua_seti(L, -2, 1);
        lua_pushinteger(L, ev->motion.x);
        lua_seti(L, -2, 2);
        lua_pushinteger(L, ev->motion.y);
        lua_seti(L, -2, 3);
        lua_pushinteger(L, ev->motion.state); //SDL_BUTTON
        lua_seti(L, -2, 4);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 5);

        lua_pushinteger(L, ev->motion.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->motion.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->motion.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushinteger(L, ev->motion.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->motion.state);
        lua_setfield(L, -2, "state");
        lua_pushinteger(L, ev->motion.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->motion.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, ev->motion.xrel);
        lua_setfield(L, -2, "xrel");
        lua_pushinteger(L, ev->motion.yrel);
        lua_setfield(L, -2, "yrel");
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: //button
        lua_createtable(L, 6, 10);
        lua_pushinteger(L, ev->button.type);
        lua_seti(L, -2, 1);
        lua_pushinteger(L, ev->button.x);
        lua_seti(L, -2, 2);
        lua_pushinteger(L, ev->button.y);
        lua_seti(L, -2, 3);
        lua_pushinteger(L, ev->button.button);
        lua_seti(L, -2, 4);
        lua_pushinteger(L, ev->button.clicks);
        lua_seti(L, -2, 5);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 6);

        lua_pushinteger(L, ev->button.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->button.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->button.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushinteger(L, ev->button.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->button.button);
        lua_setfield(L, -2, "button");
        lua_pushinteger(L, ev->button.state);
        lua_setfield(L, -2, "state");
        lua_pushinteger(L, ev->button.clicks);
        lua_setfield(L, -2, "clicks");
        lua_pushinteger(L, ev->button.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->button.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->button.y);
        lua_setfield(L, -2, "y");
        break;
    case SDL_MOUSEWHEEL: //wheel
        lua_createtable(L, 5, 11);
        lua_pushinteger(L, ev->wheel.type);
        lua_seti(L, -2, 1);
        lua_pushinteger(L, ev->wheel.y);
        lua_seti(L, -2, 2);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 3);

        lua_pushinteger(L, ev->wheel.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->wheel.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->wheel.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushinteger(L, ev->wheel.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->wheel.x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, ev->wheel.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, ev->wheel.direction);
        lua_setfield(L, -2, "direction");
#if SDL_VERSION_ATLEAST(2, 0, 18)
        lua_pushnumber(L, ev->wheel.preciseX);
        lua_setfield(L, -2, "preciseX");
        lua_pushnumber(L, ev->wheel.preciseY);
        lua_setfield(L, -2, "preciseY");
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
        lua_pushinteger(L, ev->wheel.mouseX);
        lua_setfield(L, -2, "mouseX");
        lua_pushinteger(L, ev->wheel.mouseY);
        lua_setfield(L, -2, "mouseY");
#endif
        break;
    case SDL_JOYAXISMOTION: //jaxis
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, ev->jaxis.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jaxis.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jaxis.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->jaxis.axis);
        lua_setfield(L, -2, "axis");
        lua_pushinteger(L, ev->jaxis.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->jaxis.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->jaxis.padding3);
        lua_setfield(L, -2, "padding3");
        lua_pushinteger(L, ev->jaxis.padding4);
        lua_setfield(L, -2, "padding4");
        lua_pushinteger(L, ev->jaxis.value);
        lua_setfield(L, -2, "value");
        break;
    case SDL_JOYBALLMOTION: //jball
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, ev->jball.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jball.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jball.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->jball.ball);
        lua_setfield(L, -2, "ball");
        lua_pushinteger(L, ev->jball.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->jball.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->jball.padding3);
        lua_setfield(L, -2, "padding3");
        lua_pushinteger(L, ev->jball.xrel);
        lua_setfield(L, -2, "xrel");
        lua_pushinteger(L, ev->jball.yrel);
        lua_setfield(L, -2, "yrel");
        break;
    case SDL_JOYHATMOTION: //jhat
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, ev->jhat.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jhat.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jhat.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->jhat.hat);
        lua_setfield(L, -2, "hat");
        lua_pushinteger(L, ev->jhat.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->jhat.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->jhat.value);
        lua_setfield(L, -2, "value");
        break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: //jbutton
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, ev->jbutton.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jbutton.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jbutton.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->jbutton.button);
        lua_setfield(L, -2, "button");
        lua_pushinteger(L, ev->jbutton.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->jbutton.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->jbutton.state);
        lua_setfield(L, -2, "state");
        break;
    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED: //jdevice
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, ev->jdevice.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jdevice.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jdevice.which);
        lua_setfield(L, -2, "which");
        break;
    case SDL_CONTROLLERAXISMOTION: //caxis
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, ev->caxis.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->caxis.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->caxis.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->caxis.axis);
        lua_setfield(L, -2, "axis");
        lua_pushinteger(L, ev->caxis.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->caxis.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->caxis.padding3);
        lua_setfield(L, -2, "padding3");
        lua_pushinteger(L, ev->caxis.padding4);
        lua_setfield(L, -2, "padding4");
        lua_pushinteger(L, ev->caxis.value);
        lua_setfield(L, -2, "value");
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: //cbutton
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, ev->cbutton.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->cbutton.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->cbutton.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->cbutton.button);
        lua_setfield(L, -2, "button");
        lua_pushinteger(L, ev->cbutton.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->cbutton.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->cbutton.state);
        lua_setfield(L, -2, "state");
        break;
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
    case SDL_CONTROLLERDEVICEREMAPPED: //cdevice
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, ev->jdevice.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->jdevice.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->jdevice.which);
        lua_setfield(L, -2, "which");
        break;
    case SDL_AUDIODEVICEADDED:
    case SDL_AUDIODEVICEREMOVED: //adevice
        lua_createtable(L, 0, 7);
        lua_pushinteger(L, ev->adevice.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->adevice.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->adevice.which);
        lua_setfield(L, -2, "which");
        lua_pushinteger(L, ev->adevice.iscapture);
        lua_setfield(L, -2, "iscapture");
        lua_pushinteger(L, ev->adevice.padding1);
        lua_setfield(L, -2, "padding1");
        lua_pushinteger(L, ev->adevice.padding2);
        lua_setfield(L, -2, "padding2");
        lua_pushinteger(L, ev->adevice.padding3);
        lua_setfield(L, -2, "padding3");
        break;
    case SDL_SENSORUPDATE:
    { //sensor
        int i;
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, ev->sensor.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->sensor.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->sensor.which);
        lua_setfield(L, -2, "which");
        lua_createtable(L, 6, 0);
        for (i = 0; i < 6; i++)
        {
            lua_pushnumber(L, ev->sensor.data[i]);
            lua_seti(L, -2, i + 1);
        }
        lua_setfield(L, -2, "data");
        break;
    }
    case SDL_QUIT: //quit
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, ev->quit.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->quit.timestamp);
        lua_setfield(L, -2, "timestamp");
        break;
    case SDL_SYSWMEVENT: //syswm
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, ev->syswm.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->syswm.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_newtable(L);
        lua_pushinteger(L, *(int*)&ev->syswm.msg->version);
        lua_setfield(L, -2, "version");
        lua_pushinteger(L, ev->syswm.msg->subsystem);
        lua_setfield(L, -2, "subsystem");
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, (lua_Integer)ev->syswm.msg->msg.win.hwnd);
        lua_setfield(L, -2, "hwnd");
        lua_pushinteger(L, ev->syswm.msg->msg.win.msg);
        lua_setfield(L, -2, "msg");
        lua_pushinteger(L, ev->syswm.msg->msg.win.wParam);
        lua_setfield(L, -2, "wParam");
        lua_pushinteger(L, ev->syswm.msg->msg.win.lParam);
        lua_setfield(L, -2, "lParam");
        lua_setfield(L, -2, "msg");
#endif
        break;
    case SDL_FINGERMOTION:
    case SDL_FINGERDOWN:
    case SDL_FINGERUP: //tfinger
        lua_createtable(L, 0, 10);
        lua_pushinteger(L, ev->tfinger.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->tfinger.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->tfinger.touchId);
        lua_setfield(L, -2, "touchId");
        lua_pushinteger(L, ev->tfinger.fingerId);
        lua_setfield(L, -2, "fingerId");
        lua_pushnumber(L, ev->tfinger.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, ev->tfinger.y);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, ev->tfinger.dx);
        lua_setfield(L, -2, "dx");
        lua_pushnumber(L, ev->tfinger.dy);
        lua_setfield(L, -2, "dy");
        lua_pushnumber(L, ev->tfinger.pressure);
        lua_setfield(L, -2, "pressure");
        lua_pushinteger(L, ev->tfinger.windowID);
        lua_setfield(L, -2, "windowID");
        break;
    case SDL_MULTIGESTURE: //mgesture
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, ev->mgesture.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->mgesture.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->mgesture.touchId);
        lua_setfield(L, -2, "touchId");
        lua_pushnumber(L, ev->mgesture.dTheta);
        lua_setfield(L, -2, "dTheta");
        lua_pushnumber(L, ev->mgesture.dDist);
        lua_setfield(L, -2, "dDist");
        lua_pushnumber(L, ev->mgesture.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, ev->mgesture.y);
        lua_setfield(L, -2, "y");
        lua_pushinteger(L, ev->mgesture.numFingers);
        lua_setfield(L, -2, "numFingers");
        lua_pushinteger(L, ev->mgesture.padding);
        lua_setfield(L, -2, "padding");
        break;
    case SDL_DOLLARGESTURE:
    case SDL_DOLLARRECORD: //dgesture
        lua_createtable(L, 0, 8);
        lua_pushinteger(L, ev->dgesture.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->dgesture.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->dgesture.touchId);
        lua_setfield(L, -2, "touchId");
        lua_pushinteger(L, ev->dgesture.gestureId);
        lua_setfield(L, -2, "gestureId");
        lua_pushinteger(L, ev->dgesture.numFingers);
        lua_setfield(L, -2, "numFingers");
        lua_pushnumber(L, ev->dgesture.error);
        lua_setfield(L, -2, "error");
        lua_pushnumber(L, ev->dgesture.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, ev->dgesture.y);
        lua_setfield(L, -2, "y");
        break;
    case SDL_DROPBEGIN:
    case SDL_DROPFILE: //drop
    case SDL_DROPTEXT:
    case SDL_DROPCOMPLETE: //drop
        lua_createtable(L, 3, 4);
        lua_pushinteger(L, ev->drop.type);
        lua_seti(L, -2, 1);
        lua_pushstring(L, ev->drop.file);
        lua_seti(L, -2, 2);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 3);

        lua_pushinteger(L, ev->drop.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->drop.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushstring(L, ev->drop.file);
        lua_setfield(L, -2, "file");
        lua_pushinteger(L, ev->drop.windowID);
        lua_setfield(L, -2, "windowID");
        SDL_free(ev->drop.file);
        break;
    case SDL_USEREVENT: //user
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, ev->user.type);
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, ev->user.timestamp);
        lua_setfield(L, -2, "timestamp");
        lua_pushinteger(L, ev->user.windowID);
        lua_setfield(L, -2, "windowID");
        lua_pushinteger(L, ev->user.code);
        lua_setfield(L, -2, "code");
        lua_pushlightuserdata(L, ev->user.data1);
        lua_setfield(L, -2, "data1");
        lua_pushlightuserdata(L, ev->user.data2);
        lua_setfield(L, -2, "data2");
        break;
    default:
        lua_createtable(L, 2, 1);
        lua_pushinteger(L, ev->type);
        lua_seti(L, -2, 1);
        lua_pushvalue(L, -1);
        lua_seti(L, -2, 2);
        lua_pushinteger(L, ev->type);
        lua_setfield(L, -2, "type");
        break;
    }

    return 1;
}

static int LUA_CreateEvent(lua_State* L)
{
    Uint32 type = (Uint32)luaL_optinteger(L, 1, 0);
    Uint32 windowID = (Uint32)luaL_optinteger(L, 2, 0);
    Sint32 code = (Sint32)luaL_optinteger(L, 3, 0);
    SDL_Event* ev = (SDL_Event*)lua_newuserdata(L, sizeof(SDL_Event));
    ev->user.type = type;
    ev->user.windowID = windowID;
    ev->user.code = code;
    luaL_setmetatable(L, "SDL_Event");
    return 1;
}

static int LUA_PumpEvents(lua_State* L)
{
    SDL_PumpEvents();
    return 0;
}

static int LUA_PeepEvents(lua_State* L)
{
    //SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    //int numevents = luaL_checkinteger(L, 2);
    //SDL_eventaction action = (SDL_eventaction)luaL_checkinteger(L, 3);
    //Uint32 minType = (Uint32)luaL_checkinteger(L, 4);
    //Uint32 maxType = (Uint32)luaL_checkinteger(L, 5);

    //lua_pushinteger(L, SDL_PeepEvents(ev, numevents, action, minType, maxType));
    return 1;
}

static int LUA_HasEvent(lua_State* L)
{
    Uint32 type = (Uint32)luaL_checkinteger(L, 1);
    lua_pushboolean(L, SDL_HasEvent(type));
    return 1;
}

static int LUA_HasEvents(lua_State* L)
{
    Uint32 min = (Uint32)luaL_checkinteger(L, 1);
    Uint32 max = (Uint32)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SDL_HasEvents(min, max));
    return 1;
}

static int LUA_FlushEvent(lua_State* L)
{
    Uint32 type = (Uint32)luaL_checkinteger(L, 1);

    SDL_FlushEvent(type);
    return 0;
}

static int LUA_FlushEvents(lua_State* L)
{
    Uint32 min = (Uint32)luaL_checkinteger(L, 1);
    Uint32 max = (Uint32)luaL_checkinteger(L, 2);

    SDL_FlushEvents(min, max);
    return 0;
}

static int LUA_PollEvent(lua_State* L)
{
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    lua_pushboolean(L, SDL_PollEvent(ev) != 0);
    return 1;
}

static int LUA_WaitEvent(lua_State* L)
{
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    lua_pushboolean(L, SDL_WaitEvent(ev));
    return 1;
}

static int LUA_WaitEventTimeout(lua_State* L)
{
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    int timeout = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, SDL_WaitEventTimeout(ev, timeout));
    return 1;
}

static int LUA_PushEvent(lua_State* L)
{
    SDL_Event* ev = (SDL_Event*)luaL_checkudata(L, 1, "SDL_Event");
    lua_pushboolean(L, SDL_PushEvent(ev));
    return 1;
}
//TODO
static int LUA_SetEventFilter(lua_State* L)
{

    //SDL_SetEventFilter(NULL, NULL);
    //?

    return 0;
}
//SDL_GetEventFilter

static int LUA_AddEventWatch(lua_State* L)
{

    //SDL_AddEventWatch((SDL_EventFilter)eventFilter, f);
    //?

    return 1;
}

static int LUA_DelEventWatch(lua_State* L)
{

    //SDL_DelEventWatch((SDL_EventFilter)eventFilter, f);
    //?

    return 0;
}

static int LUA_FilterEvents(lua_State* L)
{

    //SDL_FilterEvents((SDL_EventFilter)eventFilter, &f);
    //?
    return 0;
}

static int LUA_EventState(lua_State* L)
{
    Uint32 type = (Uint32)luaL_checkinteger(L, 1);
    int state = (int)lua_toboolean(L, 2);
    lua_pushboolean(L, SDL_EventState(type, state));
    return 1;
}

static int LUA_GetEventState(lua_State* L)
{
    Uint32 type = (Uint32)luaL_checkinteger(L, 1);
    lua_pushboolean(L, SDL_GetEventState(type));
    return 1;
}

static int LUA_RegisterEvents(lua_State* L)
{
    int num = (int)luaL_optinteger(L, 1, 1);

    lua_pushinteger(L, SDL_RegisterEvents(num));
    return 1;
}

static int LUA_QuitRequested(lua_State* L)
{
    lua_pushboolean(L, SDL_QuitRequested());
    return 1;
}

//SDL_SaveAllDollarTemplates
//SDL_SaveDollarTemplate

static const luaL_Reg events_funcs[] = {
    {"__index", NULL},
    {"GetEvent", LUA_GetEvent},
    {"PeepEvents", LUA_PeepEvents},
    {"PollEvent", LUA_PollEvent},
    {"WaitEvent", LUA_WaitEvent},
    {"WaitEventTimeout", LUA_WaitEventTimeout},
    {"PushEvent", LUA_PushEvent},

    {NULL, NULL} };

static const luaL_Reg sdl_funcs[] = {
    {"CreateEvent", LUA_CreateEvent},
    {"QuitRequested", LUA_QuitRequested},

    {"PumpEvents", LUA_PumpEvents},
    {"HasEvent", LUA_HasEvent},
    {"HasEvents", LUA_HasEvents},
    {"FlushEvent", LUA_FlushEvent},
    {"FlushEvents", LUA_FlushEvents},
    //{"SetEventFilter", LUA_SetEventFilter},
    //{"GetEventFilter", LUA_GetEventFilter},
    //{"AddEventWatch", LUA_AddEventWatch},
    //{"DelEventWatch", LUA_DelEventWatch},
    //FilterEvents
    {"EventState", LUA_EventState},
    {"GetEventState", LUA_GetEventState},
    {"RegisterEvents", LUA_RegisterEvents},

    {NULL, NULL} };

int bind_events(lua_State* L)
{
    luaL_newmetatable(L, "SDL_Event");
    luaL_setfuncs(L, events_funcs, 0);
    lua_pushvalue(L, -1); //ָ���Լ�
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    luaL_setfuncs(L, sdl_funcs, 0);
    return 0;
}