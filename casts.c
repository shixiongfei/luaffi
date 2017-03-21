#include <lua.h>
#include <ffi.h>
#include <stdint.h>

static
int cast_lua_pointer(lua_State *L, int i, void **pp)
{
	int ltype;
	lua_CFunction fn;
	const char *name;
	struct cfunc *func;
	struct closure *cl;

	ltype = lua_type(L, i);
	switch (ltype) {
		case LUA_TNIL:
			*pp = 0;
			break;
		case LUA_TNUMBER:
			*pp = (void *) luaL_checkinteger(L, i);
			break;
		case LUA_TSTRING:
			/* FFI discards the `const' qualifier;
			 * the C function must not change the string. */
			*pp = (void *) lua_tostring(L, i);
			break;
		case LUA_TFUNCTION:
			fn = lua_tocfunction(L, i);
			if (!fn || lua_getupvalue(L, i, 1))
				return 0; /* don't support C closure yet */
			*pp = (void *) fn;
			break;
		case LUA_TLIGHTUSERDATA:
			*pp = lua_touserdata(L, i);
			break;
		case LUA_TUSERDATA:
			/* TODO change to a __ffi_ptr metamethod call? */
			if (luaL_getmetafield(L, i, "__name") != LUA_TSTRING)
				goto fail;
			name = lua_tostring(L, -1);
			if (strcmp(name, "ffi_cvar") == 0) {
				struct ctype *typ = c_typeof_(L, i, 1);
				void *var = lua_touserdata(L, i);
				if (typ->arraysize > 0) {
					*pp = var;
				} else if (typ->type->type == FFI_TYPE_POINTER) {
					*pp = *(void **) var;
				} else {
					goto fail;
				}
			} else if (strcmp(name, "ffi_cfunc") == 0) {
				func = lua_touserdata(L, i);
				*pp = (void *) func->fn;
			} else if (strcmp(name, "ffi_closure") == 0) {
				cl = lua_touserdata(L, i);
				*pp = (void *) cl->addr;
			} else {
				goto fail;
			}
			lua_pop(L, 1); /* __name */
			break;
		default:
fail:
			return 0;
	}
	return 1;
}

static
int cast_int_c(lua_Integer n, void *addr, int type)
{
	switch (type) {
		case FFI_TYPE_UINT8: *(uint8_t *) addr = n; break;
		case FFI_TYPE_UINT16: *(uint16_t *) addr = n; break;
		case FFI_TYPE_UINT32: *(uint32_t *) addr = n; break;
		case FFI_TYPE_UINT64: *(uint64_t *) addr = n; break;
		case FFI_TYPE_SINT8: *(int8_t *) addr = n; break;
		case FFI_TYPE_SINT16: *(int16_t *) addr = n; break;
		case FFI_TYPE_SINT32: *(int32_t *) addr = n; break;
		case FFI_TYPE_SINT64: *(int64_t *) addr = n; break;

		case FFI_TYPE_FLOAT: *(float *) addr = n; break;
		case FFI_TYPE_DOUBLE: *(double *) addr = n; break;
		case FFI_TYPE_LONGDOUBLE: *(long double *) addr = n; break;

		default: return 0;
	}
	return 1;
}

static
int cast_number_c(lua_Number n, void *addr, int type)
{
	switch (type) {
		case FFI_TYPE_UINT8: *(uint8_t *) addr = n; break;
		case FFI_TYPE_UINT16: *(uint16_t *) addr = n; break;
		case FFI_TYPE_UINT32: *(uint32_t *) addr = n; break;
		case FFI_TYPE_UINT64: *(uint64_t *) addr = n; break;
		case FFI_TYPE_SINT8: *(int8_t *) addr = n; break;
		case FFI_TYPE_SINT16: *(int16_t *) addr = n; break;
		case FFI_TYPE_SINT32: *(int32_t *) addr = n; break;
		case FFI_TYPE_SINT64: *(int64_t *) addr = n; break;

		case FFI_TYPE_FLOAT: *(float *) addr = n; break;
		case FFI_TYPE_DOUBLE: *(double *) addr = n; break;
		case FFI_TYPE_LONGDOUBLE: *(long double *) addr = n; break;

		default: return 0;
	}
	return 1;
}

static
int cast_lua_c(lua_State *L, int i, void *addr, int type)
{
	int ltype;
	int rc = 0;

	ltype = lua_type(L, i);
	if (type == FFI_TYPE_POINTER) { /* pointer type is a special case */
		rc = cast_lua_pointer(L, i, (void **) addr);
	/* all below is dealing with value type */
	} else if (ltype == LUA_TBOOLEAN) {
		rc = cast_int_c(lua_toboolean(L, i), addr, type);
	} else if (ltype == LUA_TNUMBER) {
		rc = (lua_isinteger(L, i))
			? cast_int_c(lua_tointeger(L, i), addr, type)
			: cast_number_c(lua_tonumber(L, i), addr, type);
	} else if (ltype == LUA_TUSERDATA) {
		void *var = luaL_checkudata(L, i, "ffi_cvar");
		struct ctype *typ = c_typeof_(L, i, 1);
		if (type == typ->type->type) {
			memcpy(addr, var, c_sizeof_(typ));
			rc = 1;
		}
	}
	if (rc == 0) {
		lua_pushfstring(L, "expect %s, got %s",
			type_names[type],
			lua_typename(L, ltype));
		luaL_argerror(L, i, lua_tostring(L, -1));
	}
	return rc;
}

static
int cast_c_lua(lua_State *L, void *addr, int type)
{
	lua_Integer i;
	lua_Number n;
	void *p;

	switch (type) {
		case FFI_TYPE_UINT8: i = *(uint8_t *) addr; goto cast_int;
		case FFI_TYPE_SINT8: i = *(int8_t *) addr; goto cast_int;
		case FFI_TYPE_UINT16: i = *(uint16_t *) addr; goto cast_int;
		case FFI_TYPE_SINT16: i = *(int16_t *) addr; goto cast_int;
#if LUA_MAXINTEGER >= INT32_MAX
		case FFI_TYPE_SINT32: i = *(int32_t *) addr; goto cast_int;
		case FFI_TYPE_UINT32: i = *(uint32_t *) addr; goto cast_int;
#endif
#if LUA_MAXINTEGER >= INT64_MAX
		case FFI_TYPE_SINT64: i = *(int64_t *) addr; goto cast_int;
		case FFI_TYPE_UINT64: i = *(uint64_t *) addr; goto cast_int;
#endif
cast_int:
			lua_pushinteger(L, i); break;

		case FFI_TYPE_FLOAT: n = *(float *) addr; goto cast_num;
		case FFI_TYPE_DOUBLE: n = *(double *) addr; goto cast_num;
#if LUA_FLOAT_TYPE == LUA_FLOAT_LONGDOUBLE
		case FFI_TYPE_LONGDOUBLE: n = *(long double *) addr; goto cast_num;
#endif
cast_num:
			lua_pushnumber(L, n); break;

		case FFI_TYPE_POINTER:
			p = *(void **) addr;
			if (!p)
				lua_pushnil(L);
			else
				lua_pushlightuserdata(L, p);
			break;

		default:
			/* To be consistent, always push a value
			 * on to the stack.  If we are going to
			 * support STRUCT etc., it's probably
			 * OK to call makecvar_ here */
			lua_pushnil(L);
			return 0;
	}
	return 1;
}
