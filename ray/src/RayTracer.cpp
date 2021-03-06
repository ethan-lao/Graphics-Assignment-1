// The main ray tracer.

#pragma warning (disable: 4786)

#include "RayTracer.h"
#include "scene/light.h"
#include "scene/material.h"
#include "scene/ray.h"

#include "parser/Tokenizer.h"
#include "parser/Parser.h"

#include "ui/TraceUI.h"
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtx/io.hpp>
#include <string.h> // for memset

#include <iostream>
#include <fstream>

using namespace std;
extern TraceUI* traceUI;

// Use this variable to decide if you want to print out
// debugging messages.  Gets set in the "trace single ray" mode
// in TraceGLWindow, for example.
bool debugMode = false;

// Trace a top-level ray through pixel(i,j), i.e. normalized window coordinates (x,y),
// through the projection plane, and out into the scene.  All we do is
// enter the main ray-tracing method, getting things started by plugging
// in an initial ray weight of (0.0,0.0,0.0) and an initial recursion depth of 0.

glm::dvec3 RayTracer::trace(double x, double y)
{
	// Clear out the ray cache in the scene for debugging purposes,
	if (TraceUI::m_debug)
	{
		scene->clearIntersectCache();		
	}

	ray r(glm::dvec3(0,0,0), glm::dvec3(0,0,0), glm::dvec3(1,1,1), ray::VISIBILITY);
	scene->getCamera().rayThrough(x,y,r);
	double dummy;
	glm::dvec3 ret = traceRay(r, glm::dvec3(1.0,1.0,1.0), traceUI->getDepth(), dummy);
	ret = glm::clamp(ret, 0.0, 1.0);
	return ret;
}

glm::dvec3 RayTracer::tracePixel(int i, int j)
{
	glm::dvec3 col(0,0,0);

	if( ! sceneLoaded() ) return col;

	double x = double(i)/double(buffer_width);
	double y = double(j)/double(buffer_height);

	unsigned char *pixel = buffer.data() + ( i + j * buffer_width ) * 3;
	col = trace(x, y);

	pixel[0] = (int)( 255.0 * col[0]);
	pixel[1] = (int)( 255.0 * col[1]);
	pixel[2] = (int)( 255.0 * col[2]);
	return col;
}

#define VERBOSE 0

// Do recursive ray tracing!  You'll want to insert a lot of code here
// (or places called from here) to handle reflection, refraction, etc etc.
glm::dvec3 RayTracer::traceRay(ray& r, const glm::dvec3& thresh, int depth, double& t )
{
	// add base case, just return 0 vector
	if (depth < 0){
		return glm::dvec3(0.0, 0.0, 0.0);
	}

	if (thresh[0] < traceUI->getThreshold() && thresh[1] < traceUI->getThreshold() && thresh[2] < traceUI->getThreshold()) {
		return glm::dvec3(0.0, 0.0, 0.0); 
	}

	isect i;
	glm::dvec3 colorC;
#if VERBOSE
	std::cerr << "== current depth: " << depth << std::endl;
#endif

	if(scene->intersect(r, i)) {
		// YOUR CODE HERE

		// An intersection occurred!  We've got work to do.  For now,
		// this code gets the material for the surface that was intersected,
		// and asks that material to provide a color for the ray.

		// This is a great place to insert code for recursive ray tracing.
		// Instead of just returning the result of shade(), add some
		// more steps: add in the contributions from reflected and refracted
		// rays.

		const Material& m = i.getMaterial();
		t = i.getT();
		colorC = m.shade(scene.get(), r, i);

		glm::dvec3 position = r.at(i);
		glm::dvec3 d = glm::normalize(r.getDirection());
		glm::dvec3 l = -d;
		glm::dvec3 n = glm::normalize(i.getN());

		// Handle reflection
		if (m.Refl()) {
			glm::dvec3 direction = glm::normalize(d - (2 * glm::dot(d, n) * n));

			// recurse on the ray
			ray reflect = ray(position + RAY_EPSILON * direction, direction, glm::dvec3(1, 1, 1), ray::REFLECTION);
			colorC += m.kr(i) * traceRay(reflect, m.kr(i) * thresh, depth - 1, t);
		}

		// Handle refraction
		if (m.Trans()) {
			// get ratio of index of refraction
			glm::dvec3 normalSign = n;
			double n_current;
			double n_other;
			bool rayIsExiting = glm::dot(d, n) > 0;
			if (rayIsExiting) {
				// ray is leaving object
				n_current = m.index(i);
				n_other = 1;
				normalSign = -n;
			}
			else {
				// ray is entering object
				n_current = 1;
				n_other = m.index(i);
			}
			double eta = n_current / n_other;

			// get the direction
			double cosd = abs(glm::dot(normalSign, d));
			double w = eta * cosd;
			double k = 1 + (w - eta) * (w + eta);

			if(k > 0){
				glm::dvec3 direction = glm::normalize((w - sqrt(k)) * normalSign + eta * d);

				// recurse on the ray
				ray refract = ray(position + RAY_EPSILON * direction, direction, glm::dvec3(1, 1, 1), ray::REFRACTION);
				glm::dvec3 tempColor = traceRay(refract, m.kt(i) * thresh, depth - 1, t);

				colorC += m.kt(i) * tempColor;
			} else {

			}
		}
	} else {
		// No intersection.  This ray travels to infinity, so we color
		// it according to the background color, which in this (simple) case
		// is just black.
		//
		// FIXME: Add CubeMap support here.
		// TIPS: CubeMap object can be fetched from traceUI->getCubeMap();
		//       Check traceUI->cubeMap() to see if cubeMap is loaded
		//       and enabled.

		if (traceUI->cubeMap()) {
			colorC = traceUI->getCubeMap()->getColor(r);
		} else {
			colorC = glm::dvec3(0.0, 0.0, 0.0);
		}

	}
#if VERBOSE
	std::cerr << "== depth: " << depth+1 << " done, returning: " << colorC << std::endl;
#endif
	return colorC;
}

RayTracer::RayTracer()
	: scene(nullptr), buffer(0), thresh(0), buffer_width(0), buffer_height(0), m_bBufferReady(false)
{
}

RayTracer::~RayTracer()
{
}

void RayTracer::getBuffer( unsigned char *&buf, int &w, int &h )
{
	buf = buffer.data();
	w = buffer_width;
	h = buffer_height;
}

double RayTracer::aspectRatio()
{
	return sceneLoaded() ? scene->getCamera().getAspectRatio() : 1;
}

bool RayTracer::loadScene(const char* fn)
{
	ifstream ifs(fn);
	if( !ifs ) {
		string msg( "Error: couldn't read scene file " );
		msg.append( fn );
		traceUI->alert( msg );
		return false;
	}

	// Strip off filename, leaving only the path:
	string path( fn );
	if (path.find_last_of( "\\/" ) == string::npos)
		path = ".";
	else
		path = path.substr(0, path.find_last_of( "\\/" ));

	// Call this with 'true' for debug output from the tokenizer
	Tokenizer tokenizer( ifs, false );
	Parser parser( tokenizer, path );
	try {
		scene.reset(parser.parseScene());
	}
	catch( SyntaxErrorException& pe ) {
		traceUI->alert( pe.formattedMessage() );
		return false;
	} catch( ParserException& pe ) {
		string msg( "Parser: fatal exception " );
		msg.append( pe.message() );
		traceUI->alert( msg );
		return false;
	} catch( TextureMapException e ) {
		string msg( "Texture mapping exception: " );
		msg.append( e.message() );
		traceUI->alert( msg );
		return false;
	}

	if (!sceneLoaded())
		return false;

	return true;
}

void RayTracer::traceSetup(int w, int h)
{
	size_t newBufferSize = w * h * 3;
	if (newBufferSize != buffer.size()) {
		bufferSize = newBufferSize;
		buffer.resize(bufferSize);
	}
	buffer_width = w;
	buffer_height = h;
	std::fill(buffer.begin(), buffer.end(), 0);
	m_bBufferReady = true;

	/*
	 * Sync with TraceUI
	 */

	threads = traceUI->getThreads();
	block_size = traceUI->getBlockSize();
	thresh = traceUI->getThreshold();
	samples = traceUI->getSuperSamples();
	aaThresh = traceUI->getAaThreshold();

	// YOUR CODE HERE
	// FIXME: Additional initializations

	// build kd tree
	if (traceUI->kdSwitch())
		scene->buildTree(traceUI->getMaxDepth(), traceUI->getLeafSize());
}

void RayTracer::traceImageThread(int id, int w, int h) {
	for (int p = id; p < w * h; p += threads) {
		int i = (int) p / buffer_height;
		int j = p % buffer_height;
		glm::dvec3 s = tracePixel(i, j);
	}

	finishedThreads.insert(id);
}

/*
 * RayTracer::traceImage
 *
 *	Trace the image and store the pixel data in RayTracer::buffer.
 *
 *	Arguments:
 *		w:	width of the image buffer
 *		h:	height of the image buffer
 *
 */
void RayTracer::traceImage(int w, int h)
{
	// Always call traceSetup before rendering anything.
	traceSetup(w,h);

	// YOUR CODE HERE
	// FIXME: Start one or more threads for ray tracing
	//
	// TIPS: Ideally, the traceImage should be executed asynchronously,
	//       i.e. returns IMMEDIATELY after working threads are launched.
	//
	//       An asynchronous traceImage lets the GUI update your results
	//       while rendering.

	for (int t = 0; t < threads; ++t) {
		std::thread imageThread(&RayTracer::traceImageThread, this, t, w, h);
		allThreads.push_back(std::move(imageThread));
	}
}

void RayTracer::aaImageThread(int id, int w, int h) {
	double x_offset = 1.0 / double(buffer_width * samples);
	double y_offset = 1.0 / double(buffer_height * samples);

	for (int p = id; p < w * h; p += threads) {
		int i = (int) p / buffer_height;
		int j = p % buffer_height;
		
		glm::dvec3 color = getPixel(i, j);

		// check if pixel is on a boundary
		bool onBoundary = false;
		for (int a = -1; a < 2; ++a) {
			if (i + a < 0 || i + a >= buffer_width) {
				continue;
			}

			for (int b = -1; b < 2; ++b) {
				if (a == 0 && b == 0) {
					continue;
				}

				if (j + b < 0 || j + b >= buffer_height) {
					continue;
				}

				glm::dvec3 diff = abs(getPixel(i + a, j + b) - color);
				if (diff[0] > aaThresh || diff[1] > aaThresh || diff[2] > aaThresh) {
					onBoundary = true;
					break;
				}
			}
			if (onBoundary)
				break;
		}

		// if pixel is on boundary, super sample
		if (onBoundary) {
			int totalSamples = glm::pow(samples, 2);
			glm::dvec3 newColor = glm::dvec3(0, 0, 0);

			double x = (double(i) - .5) / double(buffer_width);
			double y = (double(j) - .5) / double(buffer_height);

			// loop through the grid
			for (int a = 0; a < samples; ++a) {
				double x_sample = x + (double(a) * x_offset);

				for (int b = 0; b < samples; ++b) {
					double y_sample = y + (double(b) * y_offset);
					newColor += trace(x_sample, y_sample) / double(totalSamples);
				}
			}

			// update the color
			setPixel(i, j, newColor);
		}
	}

	finishedThreads.insert(id);
}

int RayTracer::aaImage()
{
	// YOUR CODE HERE
	// FIXME: Implement Anti-aliasing here
	//
	// TIP: samples and aaThresh have been synchronized with TraceUI by
	//      RayTracer::traceSetup() function
	// return 0;

	// start aa threads
	if (samples > 0) {
		for (int t = 0; t < threads; ++t) {
			std::thread imageThread(&RayTracer::aaImageThread, this, t, buffer_width, buffer_height);
			allThreads.push_back(std::move(imageThread));
		}
	}

	return 0;
}

bool RayTracer::checkRender()
{
	// YOUR CODE HERE
	// FIXME: Return true if tracing is done.
	//        This is a helper routine for GUI.
	//
	// TIPS: Introduce an array to track the status of each worker thread.
	//       This array is maintained by the worker threads.
	
	for (int i = 0; i < threads; ++i) {
		if (finishedThreads.find(i) == finishedThreads.end())
			return false;
	}

	finishedThreads.clear();

	return true;
}

void RayTracer::waitRender()
{
	// YOUR CODE HERE
	// FIXME: Wait until the rendering process is done.
	//        This function is essential if you are using an asynchronous
	//        traceImage implementation.
	//
	// TIPS: Join all worker threads here.

	// join all threads
	for (std::thread & th : allThreads) {
		if (th.joinable()) {
        	th.join();
		}
	}

	finishedThreads.clear();
}


glm::dvec3 RayTracer::getPixel(int i, int j)
{
	unsigned char *pixel = buffer.data() + ( i + j * buffer_width ) * 3;
	return glm::dvec3((double)pixel[0]/255.0, (double)pixel[1]/255.0, (double)pixel[2]/255.0);
}

void RayTracer::setPixel(int i, int j, glm::dvec3 color)
{
	unsigned char *pixel = buffer.data() + ( i + j * buffer_width ) * 3;

	pixel[0] = (int)( 255.0 * color[0]);
	pixel[1] = (int)( 255.0 * color[1]);
	pixel[2] = (int)( 255.0 * color[2]);
}

