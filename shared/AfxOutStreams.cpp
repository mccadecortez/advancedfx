#include "stdafx.h"

#include "AfxOutStreams.h"
#include "AfxConsole.h"

#include "RawOutput.h"
#include "OpenExrOutput.h"
#include "StringTools.h"
#include "FileTools.h"

#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>

#include <hlaeFolder.h>

namespace advancedfx {

bool COutImageStream::SupplyImageFrame(double secondsSinceLastFrame, const unsigned char* pBuffer)
{
	std::wstring path;

	if (ImageFormat::ZFloat == m_ImageFormat.Format)
	{
		return CreateCapturePath(".exr", path) && WriteFloatZOpenExr(
			path.c_str(),
			pBuffer,
			m_ImageFormat.Width,
			m_ImageFormat.Height,
			sizeof(float),
			m_ImageFormat.Pitch,
			m_IfZip ? WFZOEC_Zip : WFZOEC_None,
			m_ImageFormat.Origin == ImageOrigin::TopLeft
		);
	}

	if (ImageFormat::A == m_ImageFormat.Format)
	{
		return m_IfBmpNotTga
			? CreateCapturePath(".bmp", path) && WriteRawBitmap(pBuffer, path.c_str(), m_ImageFormat.Width, m_ImageFormat.Height, 8, m_ImageFormat.Pitch, m_ImageFormat.Origin == ImageOrigin::TopLeft)
			: CreateCapturePath(".tga", path) && WriteRawTarga(pBuffer, path.c_str(), m_ImageFormat.Width, m_ImageFormat.Height, 8, true, m_ImageFormat.Pitch, 0, m_ImageFormat.Origin == ImageOrigin::TopLeft)
			;
	}

	bool isBgra = ImageFormat::BGRA == m_ImageFormat.Format;

	return m_IfBmpNotTga && !isBgra
		? CreateCapturePath(".bmp", path) && WriteRawBitmap(pBuffer, path.c_str(), m_ImageFormat.Width, m_ImageFormat.Height, 24, m_ImageFormat.Pitch, m_ImageFormat.Origin == ImageOrigin::TopLeft)
		: CreateCapturePath(".tga", path) && WriteRawTarga(pBuffer, path.c_str(), m_ImageFormat.Width, m_ImageFormat.Height, isBgra ? 32 : 24, false, m_ImageFormat.Pitch, isBgra ? 8 : 0, m_ImageFormat.Origin == ImageOrigin::TopLeft)
		;
}


bool COutImageStream::CreateCapturePath(const char* fileExtension, std::wstring& outPath)
{
	if (!m_TriedCreatePath)
	{
		m_TriedCreatePath = true;

		bool dirCreated = CreatePath(m_Path.c_str(), m_Path, true);
		if (dirCreated)
		{
			m_SucceededCreatePath = true;
		}
		else
		{
			std::string ansiString;
			if (!WideStringToUTF8String(m_Path.c_str(), ansiString)) ansiString = "[n/a]";

			advancedfx::Warning("ERROR: could not create \"%s\"\n", ansiString.c_str());
		}
	}

	if (!m_SucceededCreatePath)
		return false;

	std::wostringstream os;
	const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	os << m_Path << L"\\" << std::setfill(L'0') << std::setw(0) << epoch << "_" << std::setw(5) << m_FrameNumber << std::setw(0) << fileExtension;

	outPath = os.str();

	++m_FrameNumber;

	return true;
}


void ReplaceAllW(std::wstring& str, const std::map<std::wstring, std::wstring>& replacements)
{
	size_t start_pos = 0;
	while (true)
	{
		size_t found_pos = std::wstring::npos;
		std::map<std::wstring, std::wstring>::const_iterator itFound = replacements.end();

		for (std::map<std::wstring, std::wstring>::const_iterator itR = replacements.begin(); itR != replacements.end(); ++itR)
		{
			if (itR->first.empty()) continue;

			size_t cur_found_pos = str.find(itR->first, start_pos);

			if (cur_found_pos != std::wstring::npos && (found_pos == std::wstring::npos || cur_found_pos < found_pos))
			{
				found_pos = cur_found_pos;
				itFound = itR;
			}
		}

		if (itFound != replacements.end())
		{
			str.replace(found_pos, itFound->first.length(), itFound->second);
			start_pos = found_pos + itFound->second.length();
		}
		else break;
	}
}

BOOL AfxOutFFMPEGVideoStream_CreatePipe(
	const char* pipeName,
	OUT LPHANDLE lpReadPipe,
	OUT LPHANDLE lpWritePipe,
	IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
	DWORD nSize,
	DWORD timeOutMs,
	DWORD dwReadMode,
	DWORD dwWriteMode)
{
	HANDLE ReadPipeHandle, WritePipeHandle;
	DWORD dwError;

	// Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED

	if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	if (nSize == 0) {
		nSize = 4096;
	}

	ReadPipeHandle = CreateNamedPipeA(
		pipeName,
		PIPE_ACCESS_INBOUND | dwReadMode,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		1, // Number of pipes
		nSize, // Out buffer size
		nSize, // In buffer size
		timeOutMs, // Timeout in ms
		lpPipeAttributes
	);

	if (!ReadPipeHandle) {
		return FALSE;
	}

	WritePipeHandle = CreateFileA(
		pipeName,
		GENERIC_WRITE,
		0, // No sharing
		lpPipeAttributes,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | dwWriteMode,
		NULL                       // Template file
	);

	if (INVALID_HANDLE_VALUE == WritePipeHandle) {
		dwError = GetLastError();
		CloseHandle(ReadPipeHandle);
		SetLastError(dwError);
		return FALSE;
	}

	*lpReadPipe = ReadPipeHandle;
	*lpWritePipe = WritePipeHandle;
	return(TRUE);
}

COutFFMPEGVideoStream::COutFFMPEGVideoStream(const CImageFormat& imageFormat, const std::wstring& path, const std::wstring& ffmpegOptions, float frameRate)
	: COutVideoStream(imageFormat)
{
	std::wstring myPath(path);

	if (frameRate < 1)
	{
		advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: FPS %f < 1.\n", frameRate);
	}
	else if (!m_TriedCreatePath)
	{
		m_TriedCreatePath = true;

		bool dirCreated = CreatePath(myPath.c_str(), myPath, true);
		if (dirCreated)
		{
			m_SucceededCreatePath = true;
		}
		else
		{
			std::string ansiString;
			if (!WideStringToUTF8String(myPath.c_str(), ansiString)) ansiString = "[n/a]";

			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: could not create path \"%s\"\n", ansiString.c_str());
		}
	}

	if (m_SucceededCreatePath)
	{
		std::wstring ffmpegIni(GetHlaeFolderW());
		ffmpegIni.append(L"ffmpeg\\ffmpeg.ini");

		wchar_t ffmpegPath[MAX_PATH + 3];

		std::wstring ffmpegExe;

		int nSize = GetPrivateProfileStringW(
			L"Ffmpeg",
			L"Path",
			NULL,
			ffmpegPath,
			MAX_PATH + 3,
			ffmpegIni.c_str());
		if (0 < nSize && nSize <= MAX_PATH)
		{
			ffmpegExe.assign(ffmpegPath);
		}
		else
		{
			ffmpegExe = GetHlaeFolderW();
			ffmpegExe.append(L"ffmpeg\\bin\\ffmpeg.exe");
		}

		std::wstring pixelFormat;

		switch (imageFormat.Format)
		{
		case ImageFormat::BGR:
			pixelFormat =  L"bgr24";
			break;
		case ImageFormat::BGRA:
			pixelFormat =  L"bgra";
			break;
		case ImageFormat::A:
			pixelFormat = L"gray";
			break;
		default:
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: Unsupported image format.");
			return;
		}

		//if (imageFormat.Origin != ImageOrigin::TopLeft) ffmpegArgs << ",vflip";

		std::wstring myFFMPEGOptions(ffmpegOptions);

		// breaking change: // myPath.append(L"\\");

		std::map<std::wstring, std::wstring> replacements;
		replacements[L"{FFMPEG_PATH}"] = ffmpegExe;
		replacements[L"{AFX_STREAM_PATH}"] = myPath;
		replacements[L"{WIDTH}"] = std::to_wstring(imageFormat.Width);
		replacements[L"{HEIGHT}"] = std::to_wstring(imageFormat.Height);
		replacements[L"{PIXEL_FORMAT}"] = pixelFormat;
		replacements[L"{FRAMERATE}"] = std::to_wstring(frameRate);
		replacements[L"{QUOTE}"] = L"\"";
		replacements[L"\\{"] = L"{";
		replacements[L"\\}"] = L"}";
		replacements[L"\\\\"] = L"\\";

		ReplaceAllW(myFFMPEGOptions, replacements);

		std::wstring commandLine(myFFMPEGOptions.append(L"\0"));

		STARTUPINFOW startupInfo;

		ZeroMemory(&startupInfo, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;

		SECURITY_ATTRIBUTES saAttr;

		ZeroMemory(&saAttr, sizeof(SECURITY_ATTRIBUTES));
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		// Create STDOUT:

		std::ostringstream stOutPipeNameStream;
		stOutPipeNameStream << "\\\\.\\pipe\\AfxHookSource_FFMPEG_Out_" << GetCurrentProcessId() << "_" << (void*)this;
		std::string stdOutPipeName(stOutPipeNameStream.str());

		if (TRUE != AfxOutFFMPEGVideoStream_CreatePipe(
			stdOutPipeName.c_str(),
			&m_hChildStd_OUT_Rd,
			&m_hChildStd_OUT_Wr,
			&saAttr,
			0,
			20 * 1000,
			0, 0))
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: Could not create STDOUT.\n");
		}

		if (INVALID_HANDLE_VALUE != m_hChildStd_OUT_Rd && !SetHandleInformation(m_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream:STDOUT SetHandleInformation.\n");
			CloseHandle(m_hChildStd_OUT_Rd);
			m_hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
		}

		/*
		if (INVALID_HANDLE_VALUE == (m_OverlappedStdout.hEvent = CreateEventA(NULL, true, true, NULL)))
		{
			advancedfx::Warning("AFXERROR: CERRFFMPEGVideoStream::CERRFFMPEGVideoStream:STDOUT CreateEventA.\n");
		}
		*/

		// Create STDERR:

		std::ostringstream stErrPipeNameStream;
		stErrPipeNameStream << "\\\\.\\pipe\\AfxHookSource_FFMPEG_Err_" << GetCurrentProcessId() << "_" << (void*)this;
		std::string stdErrPipeName(stErrPipeNameStream.str());

		if (TRUE != AfxOutFFMPEGVideoStream_CreatePipe(
			stdErrPipeName.c_str(),
			&m_hChildStd_ERR_Rd,
			&m_hChildStd_ERR_Wr,
			&saAttr,
			0,
			20 * 1000,
			0, 0))
		{
			advancedfx::Warning("AFXERROR: CERRFFMPEGVideoStream::CERRFFMPEGVideoStream: Could not create STDERR.\n");
		}

		if (INVALID_HANDLE_VALUE != m_hChildStd_ERR_Rd && !SetHandleInformation(m_hChildStd_ERR_Rd, HANDLE_FLAG_INHERIT, 0))
		{
			advancedfx::Warning("AFXERROR: CERRFFMPEGVideoStream::CERRFFMPEGVideoStream:STDERR SetHandleInformation.\n");
			CloseHandle(m_hChildStd_ERR_Rd);
			m_hChildStd_ERR_Rd = INVALID_HANDLE_VALUE;
		}

		/*
		if (INVALID_HANDLE_VALUE == (m_OverlappedStderr.hEvent = CreateEventA(NULL, true, true, NULL)))
		{
			advancedfx::Warning("AFXERROR: CERRFFMPEGVideoStream::CERRFFMPEGVideoStream:STDERR CreateEventA.\n");
		}
		*/

		// Create STDIN:

		std::ostringstream stdInPipeNameStream;
		stdInPipeNameStream << "\\\\.\\pipe\\AfxHookSource_FFMPEG_In_" << GetCurrentProcessId() << "_" << (void*)this;
		std::string stdInPipeName(stdInPipeNameStream.str());

		if (TRUE != AfxOutFFMPEGVideoStream_CreatePipe(
			stdInPipeName.c_str(),
			&m_hChildStd_IN_Rd,
			&m_hChildStd_IN_Wr,
			&saAttr,
			1024 * 1024,
			20 * 1000,
			0, FILE_FLAG_OVERLAPPED))
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: Could not create STDIN.\n");
		}

		if (INVALID_HANDLE_VALUE != m_hChildStd_IN_Wr && !SetHandleInformation(m_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream:STDIN SetHandleInformation.\n");
			CloseHandle(m_hChildStd_IN_Wr);
			m_hChildStd_IN_Wr = INVALID_HANDLE_VALUE;
		}

		if (INVALID_HANDLE_VALUE == (m_OverlappedStdin.hEvent = CreateEventA(NULL, true, true, NULL)))
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream:STDIN CreateEventA.\n");
		}

		//

		ZeroMemory(&m_ProcessInfo, sizeof(m_ProcessInfo));

		m_Okay = INVALID_HANDLE_VALUE != m_hChildStd_IN_Rd
			&& INVALID_HANDLE_VALUE != m_hChildStd_IN_Wr
			&& INVALID_HANDLE_VALUE != m_hChildStd_ERR_Rd
			&& INVALID_HANDLE_VALUE != m_hChildStd_ERR_Wr
			&& INVALID_HANDLE_VALUE != m_hChildStd_OUT_Rd
			&& INVALID_HANDLE_VALUE != m_hChildStd_OUT_Wr
			? TRUE : FALSE;

		if (FALSE != m_Okay)
		{
			startupInfo.hStdInput = m_hChildStd_IN_Rd;
			startupInfo.hStdError = m_hChildStd_ERR_Wr;
			startupInfo.hStdOutput = m_hChildStd_OUT_Wr;

			m_Okay = CreateProcessW(
				ffmpegExe.c_str(),
				&(commandLine[0]),
				NULL,
				NULL,
				TRUE,
				CREATE_NO_WINDOW,
				NULL,
				NULL,
				&startupInfo,
				&m_ProcessInfo
			);

			if (TRUE != m_Okay)
			{
				advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::COutFFMPEGVideoStream: CreateProcessW.\n");
			}
		}
		if (m_Okay)
		{
		}
		else
		{
			Close();
		}
	}
	else m_Okay = FALSE;
}

void COutFFMPEGVideoStream::Close()
{
	if (INVALID_HANDLE_VALUE != m_hChildStd_IN_Wr)
	{
		CloseHandle(m_hChildStd_IN_Wr);
		m_hChildStd_IN_Wr = INVALID_HANDLE_VALUE;
	}

	if (FALSE != m_Okay)
	{
		while (HandleOutAndErr(100));
		DWORD waitCode = WaitForSingleObject(m_ProcessInfo.hProcess, 0);

		if (WAIT_OBJECT_0 != waitCode)
		{
			advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::Close.\n");
		}

		CloseHandle(m_ProcessInfo.hProcess);
		CloseHandle(m_ProcessInfo.hThread);

		m_Okay = FALSE;
	}

	if (INVALID_HANDLE_VALUE != m_hChildStd_IN_Rd)
	{
		CloseHandle(m_hChildStd_IN_Rd);
		m_hChildStd_IN_Rd = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_hChildStd_OUT_Rd)
	{
		CloseHandle(m_hChildStd_OUT_Rd);
		m_hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_hChildStd_OUT_Wr)
	{
		CloseHandle(m_hChildStd_OUT_Wr);
		m_hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_hChildStd_ERR_Rd)
	{
		CloseHandle(m_hChildStd_ERR_Rd);
		m_hChildStd_ERR_Rd = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_hChildStd_ERR_Wr)
	{
		CloseHandle(m_hChildStd_ERR_Wr);
		m_hChildStd_ERR_Wr = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_OverlappedStdin.hEvent)
	{
		CloseHandle(m_OverlappedStdin.hEvent);

		m_OverlappedStdin.hEvent = INVALID_HANDLE_VALUE;
	}
	/*
	if (INVALID_HANDLE_VALUE != m_OverlappedStdout.hEvent)
	{
		CloseHandle(m_OverlappedStdout.hEvent);

		m_OverlappedStdout.hEvent = INVALID_HANDLE_VALUE;
	}
	if (INVALID_HANDLE_VALUE != m_OverlappedStderr.hEvent)
	{
		CloseHandle(m_OverlappedStderr.hEvent);

		m_OverlappedStderr.hEvent = INVALID_HANDLE_VALUE;
	}
	*/
}

bool COutFFMPEGVideoStream::SupplyImageFrame(double secondsSinceLastFrame, const unsigned char* pBuffer)
{
	/*
	static DWORD frames = 0;
	static DWORD firstTickCount = GetTickCount();

	++frames;
	DWORD delta = GetTickCount() - firstTickCount;

	if (1000 < delta)
	{
		float fps = delta ? (float)frames / (delta / 1000.0f): 0;
		advancedfx::Message("FPS: %f\n",fps);
		frames = 0;
		firstTickCount = GetTickCount();
	}

	return true;
	*/

	if (TRUE != m_Okay) return false;

	//DWORD lastTickCount = GetTickCount();

	DWORD length = (DWORD)m_ImageFormat.Bytes;
	DWORD srcStride = m_ImageFormat.GetPixelStride();
	DWORD batchLength = (DWORD)(srcStride * m_ImageFormat.Width);
	if (batchLength == m_ImageFormat.GetLineStride()) batchLength *= (DWORD)m_ImageFormat.Height;

	while (0 < length) {

		if (WriteFile(m_hChildStd_IN_Wr, (LPCVOID)pBuffer, batchLength, NULL, &m_OverlappedStdin)) {
			length -= batchLength;
			pBuffer += batchLength;
			continue;
		}

		if (ERROR_IO_PENDING != GetLastError()) {
			Close();
			return false;
		}

		bool completed = false;

		while (!completed)
		{
			if (!HandleOutAndErr()) {
				Close();
				return false;
			}

			DWORD result = WaitForSingleObject(m_OverlappedStdin.hEvent, 1000);
			switch (result)
			{
			case WAIT_OBJECT_0:
				completed = true;
				break;
			case WAIT_TIMEOUT:
				break;
			default:
				Close();
				return false;
			}
		}

		DWORD bytesWritten;
		if (!GetOverlappedResult(m_hChildStd_IN_Wr, &m_OverlappedStdin, &bytesWritten, FALSE) || bytesWritten != length) {
			Close();
			return false;
		}

		length -= batchLength;
		pBuffer += batchLength;
	}

	return true;
}

COutFFMPEGVideoStream::~COutFFMPEGVideoStream()
{
	Close();
}

bool COutFFMPEGVideoStream::HandleOutAndErr(DWORD processWaitTimeOut)
{
	if (!m_Okay) return false;

	CHAR chBuf[251];
	DWORD bytesAvail;

	if (PeekNamedPipe(m_hChildStd_ERR_Rd, NULL, 0, NULL, &bytesAvail, NULL))
	{
		while (0 < bytesAvail)
		{
			DWORD dwBytesRead;
			if (ReadFile(m_hChildStd_ERR_Rd, chBuf, min(bytesAvail, 250), &dwBytesRead, NULL))
			{
				chBuf[dwBytesRead] = 0;
				advancedfx::Warning("%s", chBuf);
				bytesAvail -= dwBytesRead;
			}
			else
			{
				advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::HandleOutAndErr: StdErr ReadFile.\n");
				return false;
			}
		}
	}
	else
	{
		advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::HandleOutAndErr: StdErr PeekNamedPipe.\n");
		return false;
	}

	if (PeekNamedPipe(m_hChildStd_OUT_Rd, NULL, 0, NULL, &bytesAvail, NULL))
	{
		while (0 < bytesAvail)
		{
			DWORD dwBytesRead;
			if (ReadFile(m_hChildStd_OUT_Rd, chBuf, min(bytesAvail, 250), &dwBytesRead, NULL))
			{
				chBuf[dwBytesRead] = 0;
				advancedfx::Message("%s", chBuf);
				bytesAvail -= dwBytesRead;
			}
			else
			{
				advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::HandleOutAndErr: StdOut ReadFile.\n");
				return false;
			}
		}
	}
	else
	{
		advancedfx::Warning("AFXERROR: COutFFMPEGVideoStream::HandleOutAndErr: StdOut PeekNamedPipe.\n");
		return false;
	}

	// Check if FFMPEG exited:
	if (WAIT_TIMEOUT != WaitForSingleObject(m_ProcessInfo.hProcess, processWaitTimeOut))
	{
		return false;
	}

	return true;
}

// COutSamplingStream ///////////////////////////////////////////////////////

COutSamplingStream::COutSamplingStream(const CImageFormat& imageFormat, COutVideoStream* outVideoStream, float frameRate, EasySamplerSettings::Method method, double frameDuration, double exposure, float frameStrength, IImageBufferPool * imageBufferPool)
	: COutVideoStream(imageFormat)
	, m_OutVideoStream(outVideoStream)
	, m_Time(0.0)
	, m_InputFrameDuration(frameRate ? 1.0 / frameRate : 0.0)
	, m_ImageBufferPool(imageBufferPool)
{
	if (m_OutVideoStream) m_OutVideoStream->AddRef();

	unsigned int bytesPerPixel = 1;

	switch (imageFormat.Format)
	{
	case ImageFormat::BGR:
		m_EasySampler.Byte = new EasyByteSampler(EasySamplerSettings(
			imageFormat.Width * 3,
			imageFormat.Height,
			method,
			frameDuration,
			m_Time,
			exposure,
			frameStrength
		), (int)imageFormat.Pitch, this);
		break;
	case ImageFormat::BGRA:
		m_EasySampler.Byte = new EasyByteSampler(EasySamplerSettings(
			imageFormat.Width * 4,
			imageFormat.Height,
			method,
			frameDuration,
			m_Time,
			exposure,
			frameStrength
		), (int)imageFormat.Pitch, this);
		break;
	case ImageFormat::A:
		m_EasySampler.Byte = new EasyByteSampler(EasySamplerSettings(
			imageFormat.Width * 1,
			imageFormat.Height,
			method,
			frameDuration,
			m_Time,
			exposure,
			frameStrength
		), (int)imageFormat.Pitch, this);
		break;
	case ImageFormat::ZFloat:
		m_EasySampler.Float = new EasyFloatSampler(EasySamplerSettings(
			imageFormat.Width,
			imageFormat.Height,
			method,
			frameDuration,
			m_Time,
			exposure,
			frameStrength
		), this);
		break;
	default:
		advancedfx::Warning("AFXERROR: COutSamplingStream::COutSamplingStream: Unsupported image format.");
	}
}

COutSamplingStream::~COutSamplingStream()
{
	switch (m_ImageFormat.Format)
	{
	case ImageFormat::BGR:
	case ImageFormat::BGRA:
	case ImageFormat::A:
		delete m_EasySampler.Byte;
		break;
	case ImageFormat::ZFloat:
		delete m_EasySampler.Float;
	};

	if (m_OutVideoStream) m_OutVideoStream->Release();
}

bool COutSamplingStream::SupplyImageFrame(double secondsSinceLastFrame, const unsigned char* pBuffer)
{
	if (nullptr == m_OutVideoStream) return false;

	switch (m_ImageFormat.Format)
	{
	case ImageFormat::BGR:
	case ImageFormat::BGRA:
	case ImageFormat::A:
		m_EasySampler.Byte->Sample(pBuffer, m_Time);
		break;
	case ImageFormat::ZFloat:
		m_EasySampler.Float->Sample((const float*)pBuffer, m_Time);
	};

	m_Time += m_InputFrameDuration;

	return true;
}

void COutSamplingStream::Print(unsigned char const* data)
{
	if (CImageBuffer* buffer = m_ImageBufferPool->AquireBuffer())
	{
		buffer->AutoRealloc(m_ImageFormat);
		memcpy(buffer->Buffer, data, buffer->Format.Bytes);
		m_OutVideoStream->SupplyVideoData(*buffer);
		m_ImageBufferPool->ReleaseBuffer(buffer);
	}
}

void COutSamplingStream::Print(float const* data)
{
	if (CImageBuffer* buffer = m_ImageBufferPool->AquireBuffer())
	{
		buffer->AutoRealloc(m_ImageFormat);
		memcpy(buffer->Buffer, data, buffer->Format.Bytes);
		m_OutVideoStream->SupplyVideoData(*buffer);
		m_ImageBufferPool->ReleaseBuffer(buffer);
	}
}

} // namespace advancedfx {
