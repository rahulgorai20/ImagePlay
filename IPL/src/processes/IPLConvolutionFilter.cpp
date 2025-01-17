//#############################################################################
//
//  This file is part of ImagePlay.
//
//  ImagePlay is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  ImagePlay is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with ImagePlay.  If not, see <http://www.gnu.org/licenses/>.
//
//#############################################################################

#include "IPLConvolutionFilter.h"

#include <numeric>

void IPLConvolutionFilter::init()
{
    // init
    _result     = NULL;
    _offset     = 0;
    _divisor    = 0;
    _borders    = 0;
    _kernel.clear();

    // basic settings
    setClassName("IPLConvolutionFilter");
    setTitle("2D Convolution");
    setKeywords("filter");
    setCategory(IPLProcess::CATEGORY_LOCALOPERATIONS);
    setDescription("Convolution of a kernel with image.");
    setOpenCVSupport(IPLOpenCVSupport::OPENCV_OPTIONAL);

    // default values
    // 0 0 0
    // 0 1 0
    // 0 0 0
    int nrElements = 9;
    for(int i=0; i<nrElements; i++)
    {
        // set the center to 1, all others to 0
        _kernel.push_back(i == nrElements/2 ? 1 : 0);
    }

    // inputs and outputs
    addInput("Image", IPLData::IMAGE_COLOR);
    addOutput("Image", IPLData::IMAGE_COLOR);

    // properties
    addProcessPropertyVectorInt("kernel", "Kernel", "", _kernel, IPL_WIDGET_KERNEL);
    addProcessPropertyBool("normalize", "Normalize", "Divisor is computed automatically", true, IPL_WIDGET_CHECKBOXES);
    addProcessPropertyInt("divisor", "Divisor", "", 1, IPL_WIDGET_SLIDER, 1, 512);
    addProcessPropertyDouble("offset", "Offset", "", 0.0, IPL_WIDGET_SLIDER, -1.0, 1.0);
    addProcessPropertyInt("borders", "Borders:Crop|Extend|Wrap", "Wrap is not available under OpenCV.", 0, IPL_WIDGET_RADIOBUTTONS, 0, 0);
}

void IPLConvolutionFilter::destroy()
{
    delete _result;
}

bool IPLConvolutionFilter::processInputData(IPLImage* image , int, bool useOpenCV)
{
    // delete previous result
    delete _result;
    _result = NULL;

    int width = image->width();
    int height = image->height();

    // get properties
    _kernel     = getProcessPropertyVectorInt("kernel");
    _divisor    = getProcessPropertyInt("divisor");
    _offset     = getProcessPropertyDouble("offset");
    _normalize  = getProcessPropertyBool("normalize");
    _borders    = getProcessPropertyInt("borders");

    if(_normalize)
    {
        int sum = 0;
        for(size_t i=0; i<_kernel.size(); i++)
        {
            sum += _kernel[i];
        }
        _divisor = (sum != 0 ? sum : 1);
    }

    if (_divisor == 0)
    {
        addError("Invalid divisor: 0");
        return false;
    }

    float divFactor = 1.0f/_divisor;

    int kernelWidth = (int)sqrt((float)_kernel.size());
    int kernelOffset = kernelWidth / 2;

    int progress = 0;
    int maxProgress = image->height() * image->getNumberOfPlanes();

    if (!useOpenCV)
    {
        _result = new IPLImage( image->type(), width, height );
        #pragma omp parallel for default(shared)
        for( int planeNr=0; planeNr < image->getNumberOfPlanes(); planeNr++ )
        {
            IPLImagePlane* plane = image->plane( planeNr );
            IPLImagePlane* newplane = _result->plane( planeNr );

            for(int y=0; y<plane->height(); y++)
            {
                // progress
                notifyProgressEventHandler(100*progress++/maxProgress);

                for(int x=0; x<plane->width(); x++)
                {
                    float sum = 0;
                    int i = 0;
                    for( int ky=-kernelOffset; ky<=kernelOffset; ky++ )
                    {
                        for( int kx=-kernelOffset; kx<=kernelOffset; kx++ )
                        {
                            int h = _kernel[i++];
                            if( h )
                            {
                                if(_borders == 0)
                                {
                                    // Crop borders
                                    sum += plane->cp(x+kx, y+ky) * h;
                                }
                                else if(_borders == 1)
                                {
                                    // Extend borders
                                    sum += plane->bp(x+kx, y+ky) * h;
                                } else {
                                    // Wrap borders
                                    sum += plane->wp(x+kx, y+ky) * h;
                                }
                            }
                        }
                    }
                    sum = sum * divFactor + _offset;
                    sum = (sum>1.0) ? 1.0 : (sum<0) ? 0.0 : sum; // clamp to 0.0 - 1.0
                    newplane->p(x,y) = sum;
                }
            }
        }
    }
    else
    {
        notifyProgressEventHandler(-1);

        cv::Mat src = image->toCvMat();
        cv::Mat dst;
        cv::Mat kernel(kernelWidth, kernelWidth, CV_32FC1 );

        int i = 0;
        for( int y=0; y < kernelWidth; y++ )
            for( int x=0; x < kernelWidth; x++ )
                kernel.at<float>(cv::Point(x,y)) = _kernel[i++];

        kernel *= divFactor;

        static const int BORDER_TYPES[3] = {
          cv::BORDER_CONSTANT,
          cv::BORDER_REPLICATE,
          cv::BORDER_WRAP
        };

        cv::filter2D(src, dst, -1, kernel, cv::Point(-1,-1), _offset, BORDER_TYPES[_borders]);
        _result = new IPLImage(dst);
    }

    return true;
}

IPLImage* IPLConvolutionFilter::getResultData( int )
{
    return _result;
}
