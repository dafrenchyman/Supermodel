#include "R3DShader.h"
#include "Graphics/Shader.h"

namespace New3D {

static const char *vertexShaderR3D = R"glsl(

// uniforms
uniform float	fogIntensity;
uniform float	fogDensity;
uniform float	fogStart;

//outputs to fragment shader
varying float	fsFogFactor;
varying vec3	fsViewVertex;
varying vec3	fsViewNormal;		// per vertex normal vector
varying vec4	fsColor;

void main(void)
{
	fsViewVertex	= vec3(gl_ModelViewMatrix * gl_Vertex);
	fsViewNormal	= normalize(gl_NormalMatrix * gl_Normal);
	float z			= length(fsViewVertex);
	fsFogFactor		= fogIntensity * clamp(fogStart + z * fogDensity, 0.0, 1.0);

	fsColor    		= gl_Color;
	gl_TexCoord[0]	= gl_MultiTexCoord0;
	gl_Position		= gl_ModelViewProjectionMatrix * gl_Vertex;
}
)glsl";

static const char *fragmentShaderR3D = R"glsl(

uniform sampler2D tex1;			// base tex
uniform sampler2D tex2;			// micro tex (optional)

uniform bool	textureEnabled;
uniform bool	microTexture;
uniform float	microTextureScale;
uniform vec2	baseTexSize;
uniform bool	textureInverted;
uniform bool	alphaTest;
uniform bool	textureAlpha;
uniform vec3	fogColour;
uniform vec4	spotEllipse;		// spotlight ellipse position: .x=X position (screen coordinates), .y=Y position, .z=half-width, .w=half-height)
uniform vec2	spotRange;			// spotlight Z range: .x=start (viewspace coordinates), .y=limit
uniform vec3	spotColor;			// spotlight RGB color
uniform vec3	spotFogColor;		// spotlight RGB color on fog
uniform vec3	lighting[2];		// lighting state (lighting[0] = sun direction, lighting[1].x,y = diffuse, ambient intensities from 0-1.0)
uniform bool	lightEnable;		// lighting enabled (1.0) or luminous (0.0), drawn at full intensity
uniform float	specularCoefficient;// specular coefficient
uniform float	shininess;			// specular shininess
uniform float	fogAttenuation;
uniform float	fogAmbient;

//interpolated inputs from vertex shader
varying float	fsFogFactor;
varying vec3	fsViewVertex;
varying vec3	fsViewNormal;		// per vertex normal vector
varying vec4   fsColor;

vec4 GetTextureValue()
{
	vec4 tex1Data = texture2D( tex1, gl_TexCoord[0].st);

	if(textureInverted) {
		tex1Data.rgb = vec3(1.0) - vec3(tex1Data.rgb);
	}

	if (microTexture) {
		vec2 scale    = baseTexSize/256.0;
		vec4 tex2Data = texture2D( tex2, gl_TexCoord[0].st * scale * microTextureScale);
		tex1Data = (tex1Data+tex2Data)/2.0;
	}

	if (alphaTest) {
		if (tex1Data.a < (8.0/16.0)) {
			discard;
		}
	}

	if (textureAlpha == false) {
		tex1Data.a = 1.0;
	}

	return tex1Data;
}

void main()
{
	vec4 tex1Data;
	vec4 colData;
	vec4 finalData;
	vec4 fogData;

	fogData = vec4(fogColour.rgb * fogAmbient, fsFogFactor);
	tex1Data = vec4(1.0, 1.0, 1.0, 1.0);

	if(textureEnabled) {
		tex1Data = GetTextureValue();
	}

	colData = fsColor;
	finalData = tex1Data * colData;

	if (finalData.a < (1.0/16.0)) {      // basically chuck out any totally transparent pixels value = 1/16 the smallest transparency level h/w supports
		discard;
	}

	float ellipse;
	ellipse = length((gl_FragCoord.xy - spotEllipse.xy) / spotEllipse.zw);
	ellipse = pow(ellipse, 2.0);  // decay rate = square of distance from center
	ellipse = 1.0 - ellipse;      // invert
	ellipse = max(0.0, ellipse);  // clamp

	if (lightEnable) {
		vec3   lightIntensity;
		vec3   sunVector;     // sun lighting vector (as reflecting away from vertex)
		float  sunFactor;     // sun light projection along vertex normal (0.0 to 1.0)

		// Sun angle
		sunVector = lighting[0];

		// Compute diffuse factor for sunlight
		sunFactor = max(dot(sunVector, fsViewNormal), 0.0);

		// Total light intensity: sum of all components 
		lightIntensity = vec3(sunFactor*lighting[1].x + min(lighting[1].y,0.75));   // diffuse + ambient (clamped to max 0.75)

		lightIntensity = clamp(lightIntensity,0.0,1.0);

		// Compute spotlight and apply lighting
		float enable, range, d;
		float inv_r = 1.0 / spotEllipse.z; // slope of decay function

		d = spotRange.x + spotRange.y + fsViewVertex.z;
		enable = step(spotRange.x + min(spotRange.y, 0.0), -fsViewVertex.z);

		// inverse-linear falloff
		// Reference: https://imdoingitwrong.wordpress.com/2011/01/31/light-attenuation/
		// y = 1 / (d/r + 1)^2
		range = 1.0 / pow(min(0.0, d * inv_r) - 1.0, 2.0);
		range = clamp(range, 0.0, 1.0);
		range *= enable;

		float lobeEffect = range * ellipse;

		lightIntensity.rgb += spotColor*lobeEffect;

		finalData.rgb *= lightIntensity;

		if (sunFactor > 0.0 && specularCoefficient > 0.0) {
		  float nDotL = max(dot(fsViewNormal,sunVector),0.0);
		  finalData.rgb += vec3(specularCoefficient * pow(nDotL,shininess));
		}
	}

	// Spotlight on fog
	vec3 lSpotFogColor = spotFogColor * ellipse * fogColour.rgb;

	 // Fog & spotlight applied
	finalData.rgb = mix(finalData.rgb, lSpotFogColor * fogAttenuation + fogData.rgb, fogData.a);

	gl_FragColor = finalData;
}
)glsl";

R3DShader::R3DShader()
{
	m_shaderProgram		= 0;
	m_vertexShader		= 0;
	m_fragmentShader	= 0;

	Start();	// reset attributes
}

void R3DShader::Start()
{
	m_textured1			= false;
	m_textured2			= false;
	m_textureAlpha		= false;		// use alpha in texture
	m_alphaTest			= false;		// discard fragment based on alpha (ogl does this with fixed function)
	m_doubleSided		= false;
	m_lightEnabled		= false;
	m_layered			= false;
	m_textureInverted	= false;

	m_baseTexSize[0] = 0;
	m_baseTexSize[1] = 0;

	m_shininess = 0;
	m_specularCoefficient = 0;
	m_microTexScale = 0;

	m_matDet = MatDet::notset;

	m_dirtyMesh		= true;			// dirty means all the above are dirty, ie first run
	m_dirtyModel	= true;
}

bool R3DShader::LoadShader(const char* vertexShader, const char* fragmentShader)
{
	const char* vShader;
	const char* fShader;
	bool success;

	if (vertexShader) {
		vShader = vertexShader;
	}
	else {
		vShader = vertexShaderR3D;
	}

	if (fragmentShader) {
		fShader = fragmentShader;
	}
	else {
		fShader = fragmentShaderR3D;
	}

	success = LoadShaderProgram(&m_shaderProgram, &m_vertexShader, &m_fragmentShader, std::string(), std::string(), vShader, fShader);

	m_locTexture1		= glGetUniformLocation(m_shaderProgram, "tex1");
	m_locTexture2		= glGetUniformLocation(m_shaderProgram, "tex2");
	m_locTexture1Enabled= glGetUniformLocation(m_shaderProgram, "textureEnabled");
	m_locTexture2Enabled= glGetUniformLocation(m_shaderProgram, "microTexture");
	m_locTextureAlpha	= glGetUniformLocation(m_shaderProgram, "textureAlpha");
	m_locAlphaTest		= glGetUniformLocation(m_shaderProgram, "alphaTest");
	m_locMicroTexScale	= glGetUniformLocation(m_shaderProgram, "microTextureScale");
	m_locBaseTexSize	= glGetUniformLocation(m_shaderProgram, "baseTexSize");
	m_locTextureInverted= glGetUniformLocation(m_shaderProgram, "textureInverted");

	m_locFogIntensity	= glGetUniformLocation(m_shaderProgram, "fogIntensity");
	m_locFogDensity		= glGetUniformLocation(m_shaderProgram, "fogDensity");
	m_locFogStart		= glGetUniformLocation(m_shaderProgram, "fogStart");
	m_locFogColour		= glGetUniformLocation(m_shaderProgram, "fogColour");
	m_locFogAttenuation	= glGetUniformLocation(m_shaderProgram, "fogAttenuation");
	m_locFogAmbient		= glGetUniformLocation(m_shaderProgram, "fogAmbient");

	m_locLighting		= glGetUniformLocation(m_shaderProgram, "lighting");
	m_locLightEnable	= glGetUniformLocation(m_shaderProgram, "lightEnable");
	m_locShininess		= glGetUniformLocation(m_shaderProgram, "shininess");
	m_locSpecCoefficient= glGetUniformLocation(m_shaderProgram, "specularCoefficient");
	m_locSpotEllipse	= glGetUniformLocation(m_shaderProgram, "spotEllipse");
	m_locSpotRange		= glGetUniformLocation(m_shaderProgram, "spotRange");
	m_locSpotColor		= glGetUniformLocation(m_shaderProgram, "spotColor");
	m_locSpotFogColor	= glGetUniformLocation(m_shaderProgram, "spotFogColor");

	return success;
}

void R3DShader::SetShader(bool enable)
{
	if (enable) {
		glUseProgram(m_shaderProgram);
		Start();
	}
	else {
		glUseProgram(0);
	}
}

void R3DShader::SetMeshUniforms(const Mesh* m)
{
	if (m == nullptr) {
		return;			// sanity check
	}

	if (m_dirtyMesh) {
		glUniform1i(m_locTexture1, 0);
		glUniform1i(m_locTexture2, 1);
	}

	if (m_dirtyMesh || m->textured != m_textured1) {
		glUniform1i(m_locTexture1Enabled, m->textured);
		m_textured1 = m->textured;
	}

	if (m_dirtyMesh || m->microTexture != m_textured2) {
		glUniform1i(m_locTexture2Enabled, m->microTexture);
		m_textured2 = m->microTexture;
	}

	if (m_dirtyMesh || m->microTextureScale != m_microTexScale) {
		glUniform1f(m_locMicroTexScale, m->microTextureScale);
		m_microTexScale = m->microTextureScale;
	}

	if (m_dirtyMesh || m->microTexture && (m_baseTexSize[0] != m->width || m_baseTexSize[1] != m->height)) {
		m_baseTexSize[0] = (float)m->width;
		m_baseTexSize[1] = (float)m->height;
		glUniform2fv(m_locBaseTexSize, 1, m_baseTexSize);
	}

	if (m_dirtyMesh || m->inverted != m_textureInverted) {
		glUniform1i(m_locTextureInverted, m->inverted);
		m_textureInverted = m->inverted;
	}

	if (m_dirtyMesh || m->alphaTest != m_alphaTest) {
		glUniform1i(m_locAlphaTest, m->alphaTest);
		m_alphaTest = m->alphaTest;
	}

	if (m_dirtyMesh || m->textureAlpha != m_textureAlpha) {
		glUniform1i(m_locTextureAlpha, m->textureAlpha);
		m_textureAlpha = m->textureAlpha;
	}

	if (m_dirtyMesh || m->fogIntensity != m_fogIntensity) {
		glUniform1f(m_locFogIntensity, m->fogIntensity);
		m_fogIntensity = m->fogIntensity;
	}

	if (m_dirtyMesh || m->lighting != m_lightEnabled) {
		glUniform1i(m_locLightEnable, m->lighting);
		m_lightEnabled = m->lighting;
	}

	if (m_dirtyMesh || m->shininess != m_shininess) {
		glUniform1f(m_locShininess, (m->shininess + 1) * 4);
		m_shininess = m->shininess;
	}

	if (m_dirtyMesh || m->specularCoefficient != m_specularCoefficient) {
		glUniform1f(m_locSpecCoefficient, m->specularCoefficient);
		m_specularCoefficient = m->specularCoefficient;
	}

	if (m_dirtyMesh || m->layered != m_layered) {
		m_layered = m->layered;
		if (m_layered) {
			glEnable(GL_STENCIL_TEST);
		}
		else {
			glDisable(GL_STENCIL_TEST);
		}
	}

	if (m_matDet!=MatDet::zero) {

		if (m_dirtyMesh || m->doubleSided != m_doubleSided) {

			m_doubleSided = m->doubleSided;

			if (m_doubleSided) {
				glDisable(GL_CULL_FACE);
			}
			else {
				glEnable(GL_CULL_FACE);
			}
		}
	}


	m_dirtyMesh = false;
}

void R3DShader::SetViewportUniforms(const Viewport *vp)
{	
	//didn't bother caching these, they don't get frequently called anyway
	glUniform1f	(m_locFogDensity, vp->fogParams[3]);
	glUniform1f	(m_locFogStart, vp->fogParams[4]);
	glUniform3fv(m_locFogColour, 1, vp->fogParams);
	glUniform1f	(m_locFogAttenuation, vp->fogParams[5]);
	glUniform1f	(m_locFogAmbient, vp->fogParams[6]);

	glUniform3fv(m_locLighting, 2, vp->lightingParams);
	glUniform4fv(m_locSpotEllipse, 1, vp->spotEllipse);
	glUniform2fv(m_locSpotRange, 1, vp->spotRange);
	glUniform3fv(m_locSpotColor, 1, vp->spotColor);
	glUniform3fv(m_locSpotFogColor, 1, vp->spotFogColor);
}

void R3DShader::SetModelStates(const Model* model)
{
	//==========
	MatDet test;
	//==========

	test = MatDet::notset;		// happens for bad matrices with NaN

	if (model->determinant < 0)			{ test = MatDet::negative; }
	else if (model->determinant > 0)	{ test = MatDet::positive; }
	else if (model->determinant == 0)	{ test = MatDet::zero; }

	if (m_dirtyModel || m_matDet!=test) {

		switch (test) {
		case MatDet::negative:
			glCullFace(GL_FRONT);
			glEnable(GL_CULL_FACE);
			m_doubleSided = false;
			break;
		case MatDet::positive:
			glCullFace(GL_BACK);
			glEnable(GL_CULL_FACE);
			m_doubleSided = false;
			break;
		default:
			glDisable(GL_CULL_FACE);
			m_doubleSided = true;		// basically drawing on both sides now
		}
	}

	m_matDet		= test;
	m_dirtyModel	= false;
}

} // New3D
