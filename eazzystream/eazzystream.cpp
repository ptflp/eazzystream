#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <wincodec.h>
#include <iostream>
#include <fstream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Windowscodecs.lib")

using namespace std;
using namespace DirectX;

void SaveBitmapToFile(BYTE* pBitmapBits, DXGI_OUTDUPL_DESC desc, LPCWSTR filePath);

int main() {
    HRESULT hr;

    // Инициализация D3D11
    D3D_FEATURE_LEVEL featureLevel;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &featureLevel, &context);
    if (FAILED(hr)) {
        cerr << "Ошибка при создании устройства D3D11." << endl;
        return -1;
    }

    // Получение DXGI адаптера и фабрики
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    {
        IDXGIDevice1* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice));
        if (SUCCEEDED(hr)) {
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr)) {
                hr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
            }
            dxgiDevice->Release();
        }
    }

    if (FAILED(hr)) {
        cerr << "Не удалось получить DXGI адаптер и фабрику." << endl;
        return -1;
    }

    // Получение вывода и создание OutputDuplication
    IDXGIOutput1* output = nullptr;
    IDXGIOutputDuplication* outputDuplication = nullptr;
    {
        IDXGIOutput* tempOutput = nullptr;
        hr = adapter->EnumOutputs(0, &tempOutput);
        if (SUCCEEDED(hr)) {
            hr = tempOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output));
            if (SUCCEEDED(hr)) {
                hr = output->DuplicateOutput(device, &outputDuplication);
            }
            tempOutput->Release();
        }
    }

    if (FAILED(hr)) {
        cerr << "Не удалось получить вывод и создать OutputDuplication." << endl;
        return -1;
    }

    // Захват изображения
    DXGI_OUTDUPL_DESC outputDuplDesc;
    outputDuplication->GetDesc(&outputDuplDesc);
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;
    ID3D11Texture2D* acquiredDesktopImage = nullptr;
    ID3D11Texture2D* destImage = nullptr;

    hr = outputDuplication->AcquireNextFrame(INFINITE, &frameInfo, &desktopResource);
    if (SUCCEEDED(hr)) {
        hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&acquiredDesktopImage));
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC textureDesc;
            acquiredDesktopImage->GetDesc(&textureDesc);
            textureDesc.Usage = D3D11_USAGE_STAGING;
            textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            textureDesc.BindFlags = 0;
            textureDesc.MiscFlags = 0;

            hr = device->CreateTexture2D(&textureDesc, nullptr, &destImage);
            if (SUCCEEDED(hr)) {
                context->CopyResource(destImage, acquiredDesktopImage);
            }
        }
    }

    if (FAILED(hr)) {
        cerr << "Не удалось захватить изображение с экрана." << endl;
        return -1;
    }

    // Получение содержимого изображения
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = context->Map(destImage, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        cerr << "Не удалось получить содержимое изображения." << endl;
        return -1;
    }

    // Сохранение изображения в файл
    SaveBitmapToFile(static_cast<BYTE*>(mappedResource.pData), outputDuplDesc, L"CapturedImage.bmp");

    // Освобождение ресурсов
    context->Unmap(destImage, 0);
    desktopResource->Release();
    acquiredDesktopImage->Release();
    destImage->Release();
    outputDuplication->Release();
    output->Release();
    factory->Release();
    adapter->Release();
    context->Release();
    device->Release();

    wcout << "file saved to CapturedImage.bmp." << endl;

    return 0;
}

void SaveBitmapToFile(BYTE* pBitmapBits, DXGI_OUTDUPL_DESC desc, LPCWSTR filePath) {
    BITMAPFILEHEADER fileHeader = { 0 };
    fileHeader.bfType = 0x4D42; // "BM"
    fileHeader.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + desc.ModeDesc.Height * desc.ModeDesc.Width * 4);
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER infoHeader = { 0 };
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = desc.ModeDesc.Width;
    infoHeader.biHeight = -(static_cast<LONG>(desc.ModeDesc.Height));
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = desc.ModeDesc.Height * desc.ModeDesc.Width * 4;
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;

    // Открытие файла и запись заголовков
    ofstream outFile(filePath, ios::binary);
    if (outFile) {
        outFile.write(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        outFile.write(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

        // Запись данных изображения
        for (UINT row = 0; row < desc.ModeDesc.Height; ++row) {
            outFile.write(reinterpret_cast<char*>(pBitmapBits + row * desc.ModeDesc.Width * 4), desc.ModeDesc.Width * 4);
        }

        outFile.close();
    }
    else {
        cerr << "Не удалось сохранить изображение в файл." << endl;
    }
}

