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

void CaptureScreen(ID3D11Device* device, ID3D11DeviceContext* deviceContext, IDXGIOutput1* output1);
void SaveBitmapToFile(ID3D11Texture2D* texture, ID3D11DeviceContext* context, const wchar_t* filename);

int main()
{
    // Инициализация COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        std::cerr << "Failed to initialize COM." << std::endl;
        return -1;
    }
    std::cout << "Init complete" << std::endl;

    // Создание устройства и контекста
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* deviceContext = nullptr;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &deviceContext);

    if (FAILED(hr))
    {
        std::cerr << "Failed to create D3D11 device and context." << std::endl;
        CoUninitialize();
        return -1;
    }
    std::cout << "device creation complete" << std::endl;

    std::cout << "acquiring device and output" << std::endl;
    // Получение адаптера и выхода
    IDXGIOutput1* output1 = nullptr;
    {
        IDXGIDevice* dxgiDevice = nullptr;
        hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (SUCCEEDED(hr))
        {
            std::cout << "dxgiDevice acquired" << std::endl;
            IDXGIAdapter* adapter = nullptr;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr))
            {
                std::cout << "IDXGIAdapter acquired" << std::endl;
                IDXGIOutput* output = nullptr;
                hr = adapter->EnumOutputs(0, &output);
                if (SUCCEEDED(hr))
                {
                    std::cout << "IDXGIOutput acquired" << std::endl;
                    hr = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
                    output->Release();
                }
                adapter->Release();
            }
            dxgiDevice->Release();
        }
    }

    if (output1)
    {
        // Захват экрана
        CaptureScreen(device, deviceContext, output1);

        // Освобождение ресурсов
        output1->Release();
    }
    else
    {
        std::cerr << "Failed to get DXGI output." << std::endl;
    }

    // Очистка
    deviceContext->Release();
    device->Release();
    CoUninitialize();

    return 0;
}

void CaptureScreen(ID3D11Device* device, ID3D11DeviceContext* deviceContext, IDXGIOutput1* output1)
{
    DXGI_OUTPUT_DESC outputDesc;
    output1->GetDesc(&outputDesc);

    // Создаем DXGIOutputDuplication
    IDXGIOutputDuplication* outputDupl;
    HRESULT hr = output1->DuplicateOutput(device, &outputDupl);

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* acquiredDesktopImage;
    hr = outputDupl->AcquireNextFrame(0, &frameInfo, &acquiredDesktopImage);

    if (SUCCEEDED(hr))
    {
        ID3D11Texture2D* desktopImage;
        hr = acquiredDesktopImage->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&desktopImage));

        // Создаем текстуру с форматом DXGI_FORMAT_B8G8R8A8_UNORM
        D3D11_TEXTURE2D_DESC desc;
        desktopImage->GetDesc(&desc);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;

        ID3D11Texture2D* newTexture;
        device->CreateTexture2D(&desc, nullptr, &newTexture);

        // Копируем данные изображения в новую текстуру
        deviceContext->CopyResource(newTexture, desktopImage);

        // Сохраняем текстуру в файл
        const wchar_t* filename = L"CapturedImage.bmp";
        SaveBitmapToFile(newTexture, deviceContext, filename);

        // Освобождаем ресурсы
        newTexture->Release();
        desktopImage->Release();
        acquiredDesktopImage->Release();
        outputDupl->ReleaseFrame();
    }
}

void SaveBitmapToFile(ID3D11Texture2D* texture, ID3D11DeviceContext* context, const wchar_t* filename)
{
    // Получаем описание текстуры
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Создаем буфер для данных пикселей
    UINT rowPitch = desc.Width * 4; // 4 байта на пиксель (B8G8R8A8)
    UINT imageSize = rowPitch * desc.Height;
    BYTE* imageData = new BYTE[imageSize];

    // Копируем данные из текстуры в буфер
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    context->Map(texture, 0, D3D11_MAP_READ, 0, &mappedResource);
    memcpy(imageData, mappedResource.pData, imageSize);
    context->Unmap(texture, 0);

    // Инициализация Windows Imaging Component (WIC)
    IWICImagingFactory* wicFactory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));

    // Создаем WIC Bitmap
    IWICBitmap* wicBitmap;
    wicFactory->CreateBitmapFromMemory(desc.Width, desc.Height, GUID_WICPixelFormat32bppBGRA, rowPitch, imageSize, imageData, &wicBitmap);

    // Создаем файл с указанным именем файла
    IWICStream* wicStream;
    wicFactory->CreateStream(&wicStream);
    wicStream->InitializeFromFilename(filename, GENERIC_WRITE);

    // Создаем BMP энкодер
    IWICBitmapEncoder* wicEncoder;
    wicFactory->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &wicEncoder);
    wicEncoder->Initialize(wicStream, WICBitmapEncoderNoCache);

    // Создаем BMP фрейм и сохраняем изображение
    IWICBitmapFrameEncode* wicFrame;
    wicEncoder->CreateNewFrame(&wicFrame, nullptr);
    wicFrame->Initialize(nullptr);
    wicFrame->WriteSource(wicBitmap, nullptr);
    wicFrame->Commit();
    wicEncoder->Commit();

    // Очищаем ресурсы
    delete[] imageData;
    wicFrame->Release();
    wicEncoder->Release();
    wicStream->Release();
    wicBitmap->Release();
    wicFactory->Release();
}
