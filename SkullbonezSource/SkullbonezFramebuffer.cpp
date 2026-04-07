/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\'"VUUUV"'/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezFramebuffer.h"
#include <stdexcept>



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Rendering;



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
Framebuffer::Framebuffer(int width, int height)
	: fbo(0), colorTex(0), depthRBO(0), width(width), height(height)
{
	this->Build();
}



/* -- DESTRUCTOR ------------------------------------------------------------------*/
Framebuffer::~Framebuffer(void)
{
	if (this->fbo)      glDeleteFramebuffers(1,   &this->fbo);
	if (this->colorTex) glDeleteTextures(1,        &this->colorTex);
	if (this->depthRBO) glDeleteRenderbuffers(1,   &this->depthRBO);
}



/* -- BUILD -----------------------------------------------------------------------*/
void Framebuffer::Build(void)
{
	// Color texture (RGB, no mipmaps, clamped edges)
	glGenTextures(1, &this->colorTex);
	glBindTexture(GL_TEXTURE_2D, this->colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, this->width, this->height,
				 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Depth renderbuffer
	glGenRenderbuffers(1, &this->depthRBO);
	glBindRenderbuffer(GL_RENDERBUFFER, this->depthRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
						  this->width, this->height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	// Assemble FBO
	glGenFramebuffers(1, &this->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, this->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
						   GL_TEXTURE_2D, this->colorTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
							  GL_RENDERBUFFER, this->depthRBO);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		throw std::runtime_error("Framebuffer: incomplete framebuffer object");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}



/* -- BIND ------------------------------------------------------------------------*/
void Framebuffer::Bind(void) const
{
	glBindFramebuffer(GL_FRAMEBUFFER, this->fbo);
}



/* -- UNBIND ----------------------------------------------------------------------*/
void Framebuffer::Unbind(void) const
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}



/* -- GET COLOR TEXTURE -----------------------------------------------------------*/
GLuint Framebuffer::GetColorTexture(void) const
{
	return this->colorTex;
}



/* -- GET WIDTH -------------------------------------------------------------------*/
int Framebuffer::GetWidth(void) const
{
	return this->width;
}



/* -- GET HEIGHT ------------------------------------------------------------------*/
int Framebuffer::GetHeight(void) const
{
	return this->height;
}



/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void Framebuffer::ResetGLResources(void)
{
	if (this->fbo)      { glDeleteFramebuffers(1,  &this->fbo);      this->fbo = 0; }
	if (this->colorTex) { glDeleteTextures(1,       &this->colorTex); this->colorTex = 0; }
	if (this->depthRBO) { glDeleteRenderbuffers(1, &this->depthRBO); this->depthRBO = 0; }
	this->Build();
}
