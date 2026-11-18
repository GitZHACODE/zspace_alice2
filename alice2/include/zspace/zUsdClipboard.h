#pragma once
#include <windows.h>
#include <string>
#include <stdexcept>
#include <cstring>
namespace alice2 {
inline std::string readUsdClipboardText(){
 if(!OpenClipboard(nullptr))throw std::runtime_error("Clipboard is busy; try again.");
 struct Close{~Close(){CloseClipboard();}} close;
 HANDLE handle=GetClipboardData(CF_UNICODETEXT);
 if(!handle)throw std::runtime_error("Clipboard has no text. Copy USD with the zSpace shelf first.");
 const wchar_t* data=static_cast<const wchar_t*>(GlobalLock(handle));
 if(!data)throw std::runtime_error("Cannot read clipboard text.");
 const size_t capacity=GlobalSize(handle)/sizeof(wchar_t);size_t length=0;
 while(length<capacity&&data[length])++length;
 if(length>=capacity||length>size_t(INT_MAX)){GlobalUnlock(handle);throw std::runtime_error("Clipboard text is malformed or too large.");}
 int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data,int(length),nullptr,0,nullptr,nullptr);
 if(size==0&&length){GlobalUnlock(handle);throw std::runtime_error("Clipboard text has invalid Unicode.");}
 std::string result(size,'\0');if(size)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data,int(length),result.data(),size,nullptr,nullptr);
 GlobalUnlock(handle);return result;
}
inline void writeUsdClipboardText(const std::string&text){
 if(text.size()>size_t(INT_MAX))throw std::runtime_error("USD text is too large for clipboard conversion.");
 const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
 if(length<=0)throw std::runtime_error("USD text is empty or invalid UTF-8.");
 HGLOBAL buffer=GlobalAlloc(GMEM_MOVEABLE,(size_t(length)+1)*sizeof(wchar_t));
 if(!buffer)throw std::runtime_error("Cannot allocate clipboard memory.");
 auto* data=static_cast<wchar_t*>(GlobalLock(buffer));
 if(!data){GlobalFree(buffer);throw std::runtime_error("Cannot lock clipboard memory.");}
 MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),data,length);data[length]=0;GlobalUnlock(buffer);
 if(!OpenClipboard(nullptr)){GlobalFree(buffer);throw std::runtime_error("Clipboard is busy; try again.");}
 if(!EmptyClipboard()||!SetClipboardData(CF_UNICODETEXT,buffer)){CloseClipboard();GlobalFree(buffer);throw std::runtime_error("Cannot publish USD to clipboard.");}
 CloseClipboard(); // Windows now owns the allocated buffer.
}
}
