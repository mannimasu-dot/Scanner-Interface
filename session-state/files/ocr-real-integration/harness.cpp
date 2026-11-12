#include "ScannerCore.h"
#include <iostream>
#include <cstring>
using namespace scanner;
Image Render(const std::vector<std::wstring>& lines) {
 Image image; image.width=2200; image.height=1000; image.pixels.resize(size_t(image.width)*image.height*4,255);
 BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=image.width; info.bmiHeader.biHeight=-int(image.height); info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
 void* bits=nullptr; HDC dc=CreateCompatibleDC(nullptr); HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0); auto oldBitmap=SelectObject(dc,bitmap);
 PatBlt(dc,0,0,image.width,image.height,WHITENESS);
 HFONT font=CreateFontW(-68,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial"); auto oldFont=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(0,0,0));
 int y=120; for(const auto& line:lines) { if(!TextOutW(dc,100,y,line.c_str(),int(line.size()))) throw std::runtime_error("TextOut failed"); y+=120; }
 GdiFlush(); std::memcpy(image.pixels.data(),bits,image.pixels.size()); for(size_t p=3;p<image.pixels.size();p+=4) image.pixels[p]=255;
 SelectObject(dc,oldFont); SelectObject(dc,oldBitmap); DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc); image.Validate(); return image;
}
int wmain(int argc,wchar_t**argv) {
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 try {
  std::filesystem::path root=std::filesystem::absolute(argv[1]);
  auto de=Render({L"Deutscher Scanner Integrationstest",L"Grüße aus München: Straße, Äpfel und Öl.",L"Rechnungsnummer 4711, Betrag 123 Euro."});
  auto en=Render({L"English scanner integration test",L"The quick brown fox jumps over the lazy dog.",L"Invoice number 8274, total 456 dollars."});
  auto blank=Render({}); ExportOptions png; png.format=Format::Png;
  ExportImages({de},root/L"synthetic-de.png",png); ExportImages({en},root/L"synthetic-en.png",png); ExportImages({blank},root/L"synthetic-blank.png",png);
  ExportOptions o; o.searchablePdf=true; o.ocrLanguage=OcrLanguage::German;
  ExportImages({de},root/L"german.pdf",o); std::cout<<"PASS actual German OCR PDF\n";
  o.ocrLanguage=OcrLanguage::English; ExportImages({en},root/L"english.pdf",o); std::cout<<"PASS actual English OCR PDF\n";
  o.ocrLanguage=OcrLanguage::GermanEnglish; o.format=Format::PdfA;
  ExportImages({de,en,blank},root/L"mixed-three-page-pdfa.pdf",o); std::cout<<"PASS actual combined OCR PDF/A with blank third page\n";
  ExportImages({blank},root/L"blank-only-pdfa.pdf",o); std::cout<<"PASS actual blank-only OCR PDF/A\n";
  std::filesystem::create_directory(root/L"matrix");
  for(int mode=0;mode<3;++mode) for(int quality:{80,100}) { o.pdfPageMode=PdfPageMode(mode); o.quality=quality; ExportImages({de,en},root/L"matrix"/(L"archive-mode-"+std::to_wstring(mode)+L"-q"+std::to_wstring(quality)+L".pdf"),o); }
  ExportImages({de},root/L"matrix"/L"archive-empty-metadata.pdf",o);
  o.multipage=false; ExportImages({de,en},root/L"matrix"/L"archive-split.pdf",o); o.multipage=true;
  for(int kind=1;kind<=3;++kind) { Image page=de; page.kind=PixelKind(kind); ExportImages({page},root/L"matrix"/(L"archive-kind-"+std::to_wstring(kind)+L".pdf"),o); }
  std::cout<<"PASS actual OCR PDF/A validation matrix generated (kind tags only; same color pixels)\n";
  SetEnvironmentVariableW(L"SCANNER_TESSERACT",(root/L"absent-tesseract.exe").c_str()); bool rejected=false;
  try { ExportImages({de,en},root/L"missing-engine.pdf",o); } catch(const std::exception&e) { rejected=true; std::cout<<"EXPECTED missing-engine error: "<<e.what()<<"\n"; }
  if(!rejected || std::filesystem::exists(root/L"missing-engine.pdf")) throw std::runtime_error("Missing engine did not fail atomically");
  for(const auto& entry:std::filesystem::directory_iterator(root)) { auto name=entry.path().filename().wstring(); if(entry.path().extension()==L".tmp" || name.find(L".scanner-ocr-")==0) throw std::runtime_error("Staged OCR files leaked"); }
  std::cout<<"PASS missing engine rejects export without target or staged files\n";
 } catch(const std::exception&e) { std::cerr<<"FAIL "<<e.what()<<"\n"; CoUninitialize(); return 1; }
 CoUninitialize(); return 0;
}
