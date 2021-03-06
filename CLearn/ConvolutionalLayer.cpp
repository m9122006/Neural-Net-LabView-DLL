#include "stdafx.h"
#include "ConvolutionalLayer.h"
#include "BatchBuffer.h"


/* Convolutional layer Constructors
* - Layer weights for other features are stored in x-direction (second index) in the MAT layer variable.
* - NOUTX, NOUTY only refers to the dimensions of a single feature.
* - In addition to some square or rectangular structure to be convolved over, a specified number of inputs can be given to the network
which bypass the convolution. I refer to these input channels as "sidechannels".

They are intended to be passed on to some dense layer where they can be incorporated in the stream of information.
* - The CNetLayer::NOUT variable contains information about the number of features.
*/
ConvolutionalLayer::ConvolutionalLayer(size_t _NOUTX, size_t _NOUTY, size_t _NINX, size_t _NINY, size_t _kernelX, size_t _kernelY, size_t _strideY, size_t _strideX,
	size_t _outChannels, size_t _inChannels, actfunc_t type)
	: NOUTX(_NOUTX), NOUTY(_NOUTY), NINX(_NINX), NINY(_NINY), kernelX(_kernelX), kernelY(_kernelY), strideY(_strideY), strideX(_strideX), 
	inChannels(_inChannels), outChannels(_outChannels), features(_inChannels*_outChannels),
	PhysicalLayer(_outChannels*_NOUTX*_NOUTY, _inChannels*_NINX*_NINY, type, MATIND{ _kernelY, _inChannels*_outChannels*_kernelX }, MATIND{ _kernelY, _inChannels*_outChannels*_kernelX },
		MATIND{ 1,_inChannels*_outChannels }, MATIND{ _kernelY, _inChannels*_outChannels*_kernelX }) {
	// the layer matrix will act as convolutional kernel
	init();
	assertGeometry();
}

ConvolutionalLayer::ConvolutionalLayer(size_t _NOUTX, size_t _NOUTY, size_t _NINX, size_t _NINY, size_t _kernelX, size_t _kernelY, size_t _strideY, size_t _strideX,
	size_t _outChannels, size_t _inChannels, actfunc_t type, CNetLayer& lower)
	: NOUTX(_NOUTX), NOUTY(_NOUTY), NINX(_NINX), NINY(_NINY), kernelX(_kernelX), kernelY(_kernelY), strideY(_strideY), strideX(_strideX), inChannels(_inChannels), 
	outChannels(_outChannels), features(_inChannels*_outChannels), PhysicalLayer(_outChannels*_NOUTX*_NOUTY, type, MATIND{ _kernelY, _inChannels*_outChannels*_kernelX }, 
		MATIND{ _kernelY, _inChannels*_outChannels*_kernelX },
		MATIND{ 1,_inChannels*_outChannels }, MATIND{ _kernelY,_inChannels*_outChannels*_kernelX }, lower) {

	init();
	assertGeometry();
}
// second most convenient constructor
ConvolutionalLayer::ConvolutionalLayer(size_t _NOUTXY, size_t _NINXY, size_t _kernelXY, size_t _stride, size_t _outChannels, size_t _inChannels, actfunc_t type)
	: NOUTX(_NOUTXY), NOUTY(_NOUTXY), NINX(_NINXY), NINY(_NINXY), kernelX(_kernelXY), kernelY(_kernelXY), strideY(_stride), strideX(_stride), 
	inChannels(_inChannels), outChannels(_outChannels), features(_inChannels*_outChannels), PhysicalLayer(_outChannels*_NOUTXY*_NOUTXY, _inChannels*_NINXY*_NINXY, type,
		MATIND{ _kernelXY, _inChannels*_outChannels*_kernelXY }, MATIND{ _kernelXY, _inChannels*_outChannels*_kernelXY },
		MATIND{ 1, _inChannels*_outChannels}, MATIND{_kernelXY, _inChannels*_outChannels*_kernelXY}) {
	init();
	assertGeometry();
}

// most convenient constructor
ConvolutionalLayer::ConvolutionalLayer(size_t _NOUTXY, size_t _kernelXY, size_t _stride, size_t _outChannels, size_t _inChannels, actfunc_t type, CNetLayer& lower)
	: NOUTX(_NOUTXY), NOUTY(_NOUTXY), kernelX(_kernelXY), kernelY(_kernelXY), strideY(_stride), strideX(_stride), outChannels(_outChannels), inChannels(_inChannels), features(_inChannels*_outChannels),
	PhysicalLayer(_outChannels*_NOUTXY*_NOUTXY, type, MATIND{ _kernelXY, _inChannels*_outChannels*_kernelXY }, MATIND{ _kernelXY, _inChannels*_outChannels*_kernelXY },
		MATIND{ 1,_inChannels*_outChannels }, MATIND{ _kernelXY, _inChannels*_outChannels*_kernelXY }, lower) {

	// We need to know how to interpre the inputs geometrically. Thus, we request number of features.
	NINX = sqrt((lower.getNOUT()) / inChannels); // sqrt(2* 5*5/2) = 5 
	NINY = NINX; // this is only for squares.

	init();
	assertGeometry();
}
// destructor
ConvolutionalLayer::~ConvolutionalLayer() {}

layer_t ConvolutionalLayer::whoAmI() const {
	return layer_t::convolutional;
}
void ConvolutionalLayer::assertGeometry() {
	assert(outChannels*NOUTX*NOUTY == getNOUT());
	assert(NINX*NINY*inChannels == getNIN());
	// Feature dimensions
	assert((strideX*NOUTX - strideX - NINX + kernelX) % 2 == 0);
	assert((strideY*NOUTY - strideY - NINY + kernelY) % 2 == 0);
}
void ConvolutionalLayer::init() {
	W.unaryExpr(&abs<fREAL>);

	padY = padSize(NOUTY, NINY, kernelY, strideY);
	padX = padSize(NOUTX, NINX, kernelX, strideX);

	deltaSave = MAT(getNOUT(), 1);
	deltaSave.setZero();
	actSave = MAT(getNOUT(), 1);
	actSave.setZero();
}
// Select submatrix of ith feature
const MAT& ConvolutionalLayer::getIthFeature(size_t i) {
	return W._FEAT(i);
}
// weight normalization reparametrize
void ConvolutionalLayer::wnorm_setW() {
	for (uint32_t i = 0; i < features; i++) {
		fREAL vInversEntry = (VInversNorm._FEAT(i))(0, 0);
		W._FEAT(i) = G(0, i)*vInversEntry *V._FEAT(i);
	}
}

void ConvolutionalLayer::wnorm_initV() {
	V = W;
	//normalizeV();
}
void ConvolutionalLayer::wnorm_normalizeV() {
	for (uint32_t i = 0; i< features; i++)
		V._FEAT(i) *= 1.0f / normSum(V._FEAT(i));
}
void ConvolutionalLayer::wnorm_initG() {
	for (uint32_t i = 0; i< features; i++)
		G(0, i) = normSum(W._FEAT(i));
	//G.setOnes();
}
void ConvolutionalLayer::wnorm_inversVNorm() {

	VInversNorm.setOnes();
	for (uint32_t i = 0; i< features; i++)
		VInversNorm._FEAT(i) /= normSum(V._FEAT(i));
}
MAT ConvolutionalLayer::wnorm_gGrad(const MAT& grad) {
	MAT ret(1, features);

	for (uint32_t i = 0; i < features; i++) {
		fREAL vInversEntry = (VInversNorm._FEAT(i))(0, 0); // take any entry
		ret(0, i) = (grad._FEAT(i)).cwiseProduct(V._FEAT(i)).sum()*vInversEntry; //(1,1)
	}
	return ret;
}
MAT ConvolutionalLayer::wnorm_vGrad(const MAT& grad, MAT& ggrad) {
	MAT out = grad; // same dimensions as grad
					// (1) multiply rows of grad with G's
	for (uint32_t i = 0; i < features; i++) {
		fREAL vInversEntry = (VInversNorm._FEAT(i))(0, 0);
		out._FEAT(i) *= G(0, i)*vInversEntry;
		// (2) subtract 
		vInversEntry *= vInversEntry;
		out._FEAT(i) -= G(0, i)*ggrad(0, i)*V._FEAT(i)*vInversEntry;
	}
	return out;
}
/* Spectral normalization Functions
*/
void ConvolutionalLayer::snorm_setW() {
	W = W_temp / spectralNorm(W_temp, u1, v1);
}

void ConvolutionalLayer::snorm_updateUVs() {
	updateSingularVectors(W_temp, u1, v1, 1);
}
MAT ConvolutionalLayer::snorm_dWt(MAT& grad){
	// This is actual work
	if (lambdaCount>0)
		grad -= lambdaBatch / lambdaCount*(u1*v1.transpose());

	grad /= spectralNorm(W_temp, u1, v1);
	lambdaBatch = 0; // reset this
	lambdaCount = 0;
	return grad;
}
/* For/back prop
*/
void ConvolutionalLayer::forProp(MAT& inBelow, bool training, bool recursive) {


	// (1) Reshape the remaining input

	inBelow.resize(NINY, inChannels*NINX);
	inBelow = conv_(inBelow, W, NOUTY, NOUTX, strideY, strideX, padY, padX, features, outChannels, inChannels);
	inBelow.resize(getNOUT(), 1);
	inBelow += b; // add bias term

	if (training) {
		actSave = inBelow;
		inBelow = move(getACT());
		if (recursive && getHierachy() != hierarchy_t::output) {
			above->forProp(inBelow, true, true);
		}
	} else {
		inBelow = inBelow.unaryExpr(act);
		if (recursive && getHierachy() != hierarchy_t::output) {
			above->forProp(inBelow, false, true);
		}
	}
}
uint32_t ConvolutionalLayer::getOutChannels() const {
	return outChannels;
}
// Initializtion
void ConvolutionalLayer::constrainToMax(MAT & mues, MAT & maxVec) {
	//b = -mues;
	W = W / maxVec.maxCoeff(); // could be made better, but this works
}
// backprop
void ConvolutionalLayer::backPropDelta(MAT& deltaAbove, bool recursive) {

	deltaAbove = deltaAbove.cwiseProduct(getDACT());
	deltaSave = deltaAbove; // just overwrite this matrix with an (NOUT-sideChannel)-sized vector

	if (getHierachy() != hierarchy_t::input) { // ... this is not an input layer.
		
		deltaAbove.resize(NOUTY, outChannels*NOUTX); // keep track of this!!!
		deltaAbove = antiConv_(deltaAbove, W, NINY, NINX, strideY, strideX, padY, padX, features, inChannels, outChannels);
		deltaAbove.resize(getNIN(), 1);

		if (recursive) {
			below->backPropDelta(deltaAbove, true); // cascade...
		}
	}
}
// Gradient of convolution matrix
MAT ConvolutionalLayer::w_grad(MAT& input) { // deltaSave: (NOUT-sideChannel) sized vector
	deltaSave.resize(NOUTY, outChannels*NOUTX);
	if (getHierachy() == hierarchy_t::input) {

		input.resize(NINY, inChannels*NINX);

		MAT convoluted = convGrad_(input, deltaSave, kernelY, kernelX, strideY, strideX, padY, padX, features, outChannels, inChannels);
		deltaSave.resize(getNOUT(), 1); // make sure to resize to NOUT-sideChannels
		input.resize(getNIN(), 1);

		return convoluted;
	} else {
		MAT fromBelow = below->getACT();

		fromBelow.resize(NINY, inChannels*NINX);

		MAT convoluted = convGrad_(fromBelow, deltaSave, kernelY, kernelX, strideY, strideX, padY, padX, features, outChannels, inChannels);

		deltaSave.resize(getNOUT(), 1);  // make sure to resize to NOUT-sideChannels

		return convoluted;
	}
}
MAT ConvolutionalLayer::b_grad() {
	return deltaSave;
}
void ConvolutionalLayer::saveToFile(ostream& os) const {
	os << NOUTY << " " << NOUTX << " " << NINY << " " << NINX << " " << kernelY << " " << kernelX << " " << strideY << " " << strideX << " " << outChannels << " " << inChannels<< endl;
	os << spectralNormMode << " " << weightNormMode << endl;

	MAT temp = W;
	temp.resize(W.size(), 1);
	os << temp << endl;
	os << b << endl;
	if (weightNormMode) {
		temp = V;
		temp.resize(V.size(), 1);
		os << temp << endl;
		temp = G;
		temp.resize(G.size(), 1);
		os << temp << endl;
	}
}
// first line has been read already
void ConvolutionalLayer::loadFromFile(ifstream& in) {
	in >> NOUTY;
	in >> NOUTX;
	in >> NINY;
	in >> NINX;
	in >> kernelY;
	in >> kernelX;
	in >> strideY;
	in >> strideX;
	in >> outChannels;
	in >> inChannels;
	features = outChannels*inChannels;

	padY = padSize(NOUTY, NINY, kernelY, strideY);
	padX = padSize(NOUTX, NINX, kernelX, strideX);

	W = MAT(kernelY*features*kernelX, 1); // initialize as a column vector
	b = MAT(getNOUT(), 1);
	V = MAT(kernelY, features*kernelX);
	G = MAT(1, features);

	V.setZero();
	G.setZero();
	w_stepper.reset();
	b_stepper.reset();

	// Check for normalization flags
	in >> spectralNormMode;
	in >> weightNormMode;
	for (size_t i = 0; i < W.size(); ++i) {
		in >> W(i, 0);
	}
	for (size_t i = 0; i < b.size(); ++i) {
		in >> b(i, 0);
	}
	W.resize(kernelY, features*kernelX);
	if (weightNormMode) {
		V.resize(V.size(), 1);
		for (size_t i = 0; i < V.size(); ++i) {
			in >> V(i, 0);
		}
		V.resize(kernelY, features*kernelX);
		for (size_t i = 0; i < G.size(); ++i) {
			in >> G(0, i);
		}
	}
}