#pragma once

#define SHCOREAPI
#include <mime/win32/shellscalingapi.h>
#include <mime/win32/objbase.h>
#include <mime/win32/winnls.h>
#include <mime/ntuser.h>

HRESULT ScAllocString(LPCWSTR source, LPWSTR *out);
