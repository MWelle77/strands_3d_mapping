#include "DistanceWeightFunction2PPR.h"

namespace reglib{

DistanceWeightFunction2PPR::DistanceWeightFunction2PPR(	double maxd_, int histogram_size_){
	update_size = true;

	start_maxd = maxd_;
	regularization		= 0.1;
	maxd 				= maxd_;
	histogram_size		= histogram_size_;
	blurval				= 5;
	stdval				= blurval;
	stdgrow				= 1.1;

	//printf("maxd: %5.5f histogram_size:%i\n",maxd,histogram_size);
	noiseval = 100.0;
	
	prob.resize(histogram_size+1);
	prob[histogram_size] = 0;
	
	histogram.resize(histogram_size+1);
	blur_histogram.resize(histogram_size+1);
	noise.resize(histogram_size+1);

	startreg = regularization;
	noiseval = maxd_;



	threshold = false;
	uniform_bias = true;
	debugg_print = false;
	scale_convergence = true;
	nr_inliers = 1;

	update_size = true;
	target_length = 5.0;
	data_per_bin = 10;

	meanoffset = std::max(0.0,(maxd_ -regularization -noiseval)/target_length);
}
DistanceWeightFunction2PPR::~DistanceWeightFunction2PPR(){}

inline double exp1(double x) {
  x = 1.0 + x / 256.0;
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x; x *= x; x *= x;
  return x;
}

inline double exp2(double x) {
  x = 1.0 + x / 1024;
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x;
  return x;
}

inline double usedExp(double x){
	return exp(x);
}

class Gaussian {
	public:
	double mul;
	double mean;
	double stdval;
	double scaledinformation;
	void update(){scaledinformation = -0.5/(stdval*stdval);}
	
	Gaussian(double mul_, double mean_,	double stdval_){
		mul = mul_;
		mean = mean_;
		stdval = stdval_;
		update();
	}
	
	double getval(double x){
		double dx = mean-x;
		return mul*usedExp(dx*dx*scaledinformation);
	}
};

class gaussian2 {
	public:
	gaussian2(double mean, double mul, double x, double y) : mean_(mean), mul_(mul), x_(x), y_(y) {}
	bool operator()(const double* const p,double* residuals) const {
		 double stddiv = p[0];
		if(mul_ > 0){
			double dx = x_-mean_;
			residuals[0]  = 0;
			residuals[0] += mul_*usedExp(-0.5*dx*dx/(stddiv*stddiv));
			residuals[0] -= y_;
			if(residuals[0] > 0){	residuals[0] =  sqrt( 5.0*residuals[0]);}
			else{					residuals[0] = -sqrt(-residuals[0]);}
		}else{residuals[0] = 99999999999999999999999.0;}
		return true;
	}
	private:
	const double mean_;
	const double mul_;
	const double x_;
	const double y_;
};

double getCurrentTime2(){
	struct timeval start;
	gettimeofday(&start, NULL);
	double sec = start.tv_sec;
	double usec = start.tv_usec;
	double t = sec+usec*0.000001;
	return t;
}


//multiple optimization obvious
void blurHistogram(std::vector<float> & blur_hist,  std::vector<float> & hist, float stdval){
	int nr_data = blur_hist.size();
	double info = -0.5/(stdval*stdval);
	
	double weights[nr_data];
	for(int i = 0; i < nr_data; i++){weights[i] = usedExp(i*i*info);}
	
	int offset = 3.0*stdval;
	offset = std::max(3,offset);

	for(int i = 0; i < nr_data; i++){
		double sumV = 0;
		double sumW = 0;
		int start	= std::max(0,i-offset);
		int stop	= std::min(nr_data,i+offset+1);
		for(int j = start; j < stop; j++){
			double w = weights[abs(i-j)];//usedExp(dx*dx*info);
			double v = hist[j];
			sumV += w*v;
			sumW += w;
		}
		blur_hist[i] = sumV/sumW;
	}
}


const double step_h = 0.00001;
const unsigned int step_iter = 20;
const double cutoff_exp = -12;

double scoreCurrent(double bias, double mul, double mean, double stddiv, std::vector<float> & X, std::vector<float> & Y, unsigned int nr_data){
	double info = -0.5/(stddiv*stddiv);
	double sum = 0;
	double sum2 = 0;
	for(unsigned int i = 0; i < nr_data; i++){
		double dx = X[i] - mean;
		double inp = info*dx*dx;
		if(inp < cutoff_exp){
			sum += Y[i];
		}else{
			double diff = mul*usedExp(info*dx*dx) - Y[i];
			if(diff > 0){
				sum += 3.0*diff;
			}else{
				sum -= diff;//*diff;
			}
		}
	}
	return sum;
}



double fitStdval(double bias, double mul, double mean, std::vector<float> & X, std::vector<float> & Y, unsigned int nr_data){
	int iter = 25;

	double ysum = 0;
	for(unsigned int i = 0; i < nr_data; i++){ysum += fabs(Y[i]);}

	double std_mid = 0;
	for(unsigned int i = 0; i < nr_data; i++){std_mid += (X[i]-mean)*(X[i]-mean)*fabs(Y[i]-bias)/ysum;}

	std_mid = sqrt(std_mid);
	double std_max = std_mid*2;
	double std_min = 0;

	for(int i = 0; i < step_iter; i++){
		std_mid = (std_max+std_min)/2;
		double std_neg = scoreCurrent(bias,mul,mean,std_mid-step_h,X,Y,nr_data);
		double std_pos = scoreCurrent(bias,mul,mean,std_mid+step_h,X,Y,nr_data);

		if(std_neg < std_pos){	std_max = std_mid;}
		else{					std_min = std_mid;}
	}
	return std_mid;
}

double fitBias(double bias, double mul, double mean, double std_mid, std::vector<float> & X, std::vector<float> & Y, unsigned int nr_data){
	int iter = 25;
	double h = 0.000000001;


	double bias_max = bias*2;
	double bias_min = 0;

	for(int i = 0; i < iter; i++){
		bias = (bias_max+bias_min)/2;
		double std_neg = scoreCurrent(bias-step_h,mul,mean,std_mid,X,Y,nr_data);
		double std_pos = scoreCurrent(bias+step_h,mul,mean,std_mid,X,Y,nr_data);

		if(std_neg < std_pos){	bias_max = bias;}
		else{					bias_min = bias;}
	}
	return bias;
}

double fitMean(double bias,double mul, double mean, double std_mid, std::vector<float> & X, std::vector<float> & Y, unsigned int nr_data){
	int iter = 25;
	double h = 0.000000001;
	double mean_max = mean*2;
	double mean_min = 0;

	for(int i = 0; i < step_iter; i++){
		mean = (mean_max+mean_min)/2;
		double std_neg = scoreCurrent(bias,mul,mean-step_h,std_mid,X,Y,nr_data);
		double std_pos = scoreCurrent(bias,mul,mean+step_h,std_mid,X,Y,nr_data);

		if(std_neg < std_pos){	mean_max = mean;}
		else{					mean_min = mean;}
	}
	return mean;
}

double fitMul(double bias, double mul, double mean, double std_mid, std::vector<float> & X, std::vector<float> & Y, unsigned int nr_data){
	int iter = 25;
	double h = 0.000000001;
	double mul_max = mul*2;
	double mul_min = 0;

	for(int i = 0; i < step_iter; i++){
		mul = (mul_max+mul_min)/2;
		double std_neg = scoreCurrent(bias,mul-step_h,mean,std_mid,X,Y,nr_data);
		double std_pos = scoreCurrent(bias,mul+step_h,mean,std_mid,X,Y,nr_data);

		if(std_neg < std_pos){	mul_max = mul;}
		else{					mul_min = mul;}
	}
	return mul;
}

Gaussian getModel(double & stdval,std::vector<float> & hist, bool uniform_bias){
	double mul = hist[0];
	double mean = 0;
	unsigned int nr_bins = hist.size();
	
	for(unsigned int k = 1; k < nr_bins; k++){
		if(hist[k] > mul){
			mul = hist[k];
			mean = k;
		}
	}

	std::vector<float> X;
	std::vector<float> Y;
	for(unsigned int k = 0; k < nr_bins; k++){
		if(hist[k]  > mul*0.01){
			X.push_back(k);
			Y.push_back(hist[k]);
		}
	}

	unsigned int nr_data_opt = X.size();
	double bias = 0;
	if(uniform_bias){
		stdval = 0.01;
		double ysum = 0;
		for(unsigned int i = 0; i < nr_data_opt; i++){
			bias += fabs(Y[i]);
		}
		bias /= nr_data_opt;
	}

	for(int i = 0; i < 1; i++){
		//if(uniform_bias){bias = fitStdval(bias, mul,mean,X,Y,nr_data_opt);}
		stdval = fitStdval(bias, mul,mean,X,Y,nr_data_opt);
		//mean = fitMean(bias, mul,mean,stdval,X,Y,nr_data_opt);
		//mul = fitMul(bias, mul,mean,stdval,X,Y,nr_data_opt);
	}

	return Gaussian(mul,mean,stdval);
}

double DistanceWeightFunction2PPR::getNoise(){return regularization+noiseval;}// + stdval*double(histogram_size)/maxd;}

void DistanceWeightFunction2PPR::computeModel(MatrixXd mat){
//printf("void DistanceWeightFunction2PPR::computeModel(MatrixXd mat)\n");

	const unsigned int nr_data = mat.cols();
	const int nr_dim = mat.rows();
	double start_time = getCurrentTime2();


	if(update_size){
		maxd = (getNoise()+meanoffset)*target_length;
	}

	int nr_inside = 0;
	for(unsigned int j = 0; j < nr_data; j++){
		for(int k = 0; k < nr_dim; k++){
			if(fabs(mat(k,j)) < maxd){nr_inside++;}
		}
	}

	if(update_size){
		histogram_size = std::min(int(prob.size()),std::max(10,std::min(1000,int(float(nr_inside)/data_per_bin))));
		blurval = 0.01*double(histogram_size);
	}

	//printf("maxd %f histogram_size: %i blurval %f\n",maxd,histogram_size,blurval);

	const float histogram_mul = float(histogram_size)/maxd;
/*
	for(unsigned int j = 0; j < nr_data; j++){
		for(int k = 0; k < nr_dim; k++){
			int ind = fabs(mat(k,j))*histogram_mul;
			if(ind >= 0 && ind < histogram_size){histogram[ind]++;}
		}
	}
*/

	for(int j = 0; j < histogram_size; j++){histogram[j] = 0;}
	for(unsigned int j = 0; j < nr_data; j++){
		for(int k = 0; k < nr_dim; k++){
			int ind = fabs(mat(k,j))*histogram_mul;
			if(ind >= 0 && ind < histogram_size){histogram[ind]++;}
		}
	}

	start_time = getCurrentTime2();
	blurHistogram(blur_histogram,histogram,blurval);
	
	Gaussian g = getModel(stdval,blur_histogram,uniform_bias);

	stdval	= g.stdval;
	mulval	= g.mul;
	meanval	= g.mean;

	noiseval = maxd*g.stdval/float(histogram_size);
	meanoffset = maxd*g.mean/float(histogram_size);

	g.stdval += histogram_size*regularization/maxd;
	g.update();
	
	for(int k = 0; k < histogram_size; k++){	noise[k] = g.getval(k);}
	
	double maxp = 0.99;
	for(int k = 0; k < histogram_size; k++){
		//if(k < g.mean){	prob[k] = maxp;	}
		//else{
			double hs = blur_histogram[k] +0.0000001;
			prob[k] = std::min(maxp , noise[k]/hs);//never fully trust any data
		//}
	}
/*
	for(int k = 0; k < histogram_size; k++){
		if(k < g.mean){	prob[k] = maxp;	}
		else{
			double hs = blur_histogram[k] +0.0000001;
			prob[k] = std::min(maxp , noise[k]/hs);//never fully trust any data
		}
	}
*/
	double next_maxd = log2(((getNoise()+meanoffset)*target_length)/maxd);

	if(debugg_print){printf("###############################################################################################################\n");}
	if(debugg_print){printf("maxd: %f histogram_size: %i blurval: %f \n",maxd,histogram_size,blurval);}
	if(debugg_print){printf("log2(next_maxd) %f\n",next_maxd);}
	if(debugg_print){printf("regularization: %f\n",regularization);}
	if(debugg_print){printf("estimated: %f\n",maxd*stdval/float(histogram_size));}
	if(debugg_print){printf("meanoffset: %f\n",meanoffset);}
	if(debugg_print){printf("hist = [");			for(unsigned int k = 0; k < 100 && k < histogram_size; k++){printf("%i ",int(histogram[k]));}		printf("];\n");}
	if(debugg_print){printf("noise = [");			for(unsigned int k = 0; k < 100 && k < histogram_size; k++){printf("%i ",int(noise[k]));}			printf("];\n");}
	if(debugg_print){printf("hist_smooth = [");		for(unsigned int k = 0; k < 100 && k < histogram_size; k++){printf("%i ",int(blur_histogram[k]));}	printf("];\n");}
	if(debugg_print){printf("prob = [");			for(unsigned int k = 0; k < 100 && k < histogram_size; k++){printf("%2.2f ",prob[k]);}				printf("];\n");}
	if(debugg_print){printf("###############################################################################################################\n");}

	if(update_size){
		if(fabs(next_maxd) > 1){computeModel(mat);}
	}

	//if(debugg_print){printf("end DistanceWeightFunction2PPR::computeModel\n");exit(0);}
/*
	if(update_size){
		printf("maxd: %f nr_data: %i nr_bins: %i\n",maxd,nr_data,histogram_size);
		printf("regularization: %f\n",regularization);
		printf("estimated: %f\n",maxd*stdval/float(histogram_size));
		printf("nr_data: %i data_per_bin: %f estimated nr bins: %i\n",nr_data,data_per_bin,int(float(nr_data)/data_per_bin));
		//printf("new nr_bins: %i noiseval: %f regularization: %f\n",nr_data/data_per_bin,noiseval,regularization);
		int new_histogram_size = std::min(int(prob.size()),std::max(10,std::min(1000,int(float(nr_data)/data_per_bin))));
		double new_maxd = getNoise()*target_length;
		printf("new_maxd: %f\n",new_maxd);
		printf("new_histogram_size: %i\n",new_histogram_size);

	}
	//target_length = 10.0;
	//data_per_bin = 20;
	exit(0);
	*/
}

VectorXd DistanceWeightFunction2PPR::getProbs(MatrixXd mat){
	//printf("debugg_print: %i\n",debugg_print);
	//exit(0);
	const unsigned int nr_data = mat.cols();
	const int nr_dim = mat.rows();
	const float histogram_mul = float(histogram_size)/maxd;

	nr_inliers = 0;
	VectorXd weights = VectorXd(nr_data);
	for(unsigned int j = 0; j < nr_data; j++){
		float inl  = 1;
		float ninl = 1;
		for(int k = 0; k < nr_dim; k++){
			int ind = fabs(mat(k,j))*histogram_mul;
			float p = 0;
			if(ind >= 0 && ind < histogram_size){p = prob[ind];}
			inl *= p;
			ninl *= 1.0-p;
		}
		double d = inl / (inl+ninl);
		nr_inliers += d;
		weights(j) = d;
	}
	//printf("sum: %f\n",sum);

	if(threshold){
		for(unsigned int j = 0; j < nr_data; j++){
			weights(j) = weights(j) > 0.5;
		}
	}
	return weights;
}

void DistanceWeightFunction2PPR::update(){
	//if(debugg_print){printf("###############################################################################################################\n");}
	//if(debugg_print){printf("hist = [");			for(unsigned int k = 0; k < 100; k++){printf("%i ",int(histogram[k]));}		printf("];\n");}
	//if(debugg_print){printf("noise = [");			for(unsigned int k = 0; k < 100; k++){printf("%i ",int(noise[k]));}			printf("];\n");}
	//if(debugg_print){printf("hist_smooth = [");		for(unsigned int k = 0; k < 100; k++){printf("%i ",int(blur_histogram[k]));}	printf("];\n");}
	//if(debugg_print){printf("prob = [");			for(unsigned int k = 0; k < 100; k++){printf("%2.2f ",prob[k]);}				printf("];\n");}
	//if(debugg_print){printf("###############################################################################################################\n");}

	if(true || debugg_print){
		std::vector<float> new_histogram = histogram;
		std::vector<float> new_blur_histogram = blur_histogram;

		float old_sum_prob = 0;
		for(unsigned int k = 0; k < histogram_size; k++){old_sum_prob += prob[k] * histogram[k];}

		double maxp = 0.99;
		Gaussian g = Gaussian(mulval,meanval,stdval);//getModel(stdval,blur_histogram,uniform_bias);

		int iteration = 0;
		while(true){
			iteration++;
			regularization *= 0.5;
			double change = histogram_size*regularization/maxd;
			if(change < 0.001*stdval){break;}

			g.stdval += change;
			g.update();

			float new_sum_prob = 0;
			for(int k = 0; k < histogram_size; k++){
				double hs = new_blur_histogram[k] +0.0000001;
				new_sum_prob += std::min(maxp , g.getval(k)/hs) * new_histogram[k];
			}

			if(new_sum_prob < 0.999*old_sum_prob){break;}
			g.stdval -= change;
			g.update();
		}
	}else{
		regularization *= 0.5;
	}
	//exit(0);
}
void DistanceWeightFunction2PPR::reset(){
	regularization = startreg;
	noiseval = start_maxd;
	meanoffset = std::max(0.0,(start_maxd -regularization -noiseval)/target_length);
}

std::string DistanceWeightFunction2PPR::getString(){
	char buf [1024];

	if(startreg != 0){
		sprintf(buf,"PPRreg%15.15ld",long (1000.0*start_maxd));
		//return "PPRreg";
	}else{
		sprintf(buf,"PPR%15.15ld",long (1000.0*start_maxd));
		//return "PPR";
	}
	return std::string(buf);
}

double DistanceWeightFunction2PPR::getConvergenceThreshold(){

	//double change = histogram_size*regularization/maxd;
	//if(change < 0.01*stdval){printf("break becouse of convergence\n");break;}
	//if(change < 0.01*stdval){break;}
	if(scale_convergence){
		return convergence_threshold*(regularization + maxd*stdval/double(histogram_size))/sqrt(nr_inliers);
	}else{
		return convergence_threshold;
	}
	//return convergence_threshold;
}
}


