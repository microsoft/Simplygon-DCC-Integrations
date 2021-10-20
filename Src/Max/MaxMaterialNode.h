#pragma once

class MaterialTextureOverride
{
	public:
	std::basic_string<TCHAR> MaterialName;
	std::basic_string<TCHAR> TextureFileName;
	std::basic_string<TCHAR> MappingChannelName;
	bool IsSRGB;

	MaterialTextureOverride() { this->IsSRGB = false; }
};

class MaterialTextureMapChannelOverride
{
	public:
	std::basic_string<TCHAR> MaterialName;
	std::basic_string<TCHAR> MappingChannelName;
	int MappingChannel;

	MaterialTextureMapChannelOverride() { this->MappingChannel = 0; }
};

class MaterialColorOverride
{
	public:
	std::basic_string<TCHAR> MaterialName;
	std::basic_string<TCHAR> MappingChannelName;
	float ColorValue[ 4 ];
	void SetColorRGBA( float r, float g, float b, float a )
	{
		this->ColorValue[ 0 ] = r;
		this->ColorValue[ 1 ] = g;
		this->ColorValue[ 2 ] = b;
		this->ColorValue[ 3 ] = a;
	}
	float GetR() { return ColorValue[ 0 ]; }
	float GetG() { return ColorValue[ 1 ]; }
	float GetB() { return ColorValue[ 2 ]; }
	float GetA() { return ColorValue[ 3 ]; }

	MaterialColorOverride()
	{
		this->ColorValue[ 0 ] = 0.f;
		this->ColorValue[ 1 ] = 0.f;
		this->ColorValue[ 2 ] = 0.f;
		this->ColorValue[ 3 ] = 0.f;
	}
};