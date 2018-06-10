#pragma once

// by simon yeung, 02/06/2018
// all rights reserved

#include <math.h>
#include <stdlib.h>

inline float randf(){
	return ((float)rand())/((float)RAND_MAX);
}

inline float	randf(float min, float max){
	return min + (max - min) * randf();
}

struct int2
{
	int x;
	int y;
};

struct Vector2
{
	float x;
	float y;

	Vector2() {}

	Vector2(float _x, float _y)
	{
		x = _x;
		y = _y;
	}
};

struct Vector3
{
	float x;
	float y;
	float z;

	Vector3() {}

	Vector3(float _x, float _y, float _z)
	{
		x = _x;
		y = _y;
		z = _z;
	}
	Vector3 operator* (float s) const
	{
		return Vector3(x*s, y*s, z*s);
	}
	
	void operator*= (float s) {
		x *= s;
		y *= s;
		z *= s;
	}
	void operator+= (const Vector3& v) {
		x += v.x;
		y += v.y;
		z += v.z;
	}
	Vector3 operator- (const Vector3& v) const {
		return Vector3(x - v.x, y - v.y, z - v.z);
	}
	Vector3 operator+ (const Vector3& v) const {
		return Vector3(x + v.x, y + v.y, z + v.z);
	}

	float length2() const {
		return x * x + y * y + z * z;
	}

	float length() const {
		return sqrtf(length2());
	}
	void normalize() {
		float s = 1.0f / length();
		(*this) *= s;
	}

	float dot(const Vector3& v) const {
		return x * v.x + y * v.y + z * v.z;
	}

	Vector3 cross(const Vector3& v) const {
		return Vector3(	y * v.z - z * v.y,
						z * v.x - x * v.z,
						x * v.y - y * v.x);
	}
};

struct Vector4
{
	float x;
	float y;
	float z;
	float w;

	Vector4(){}
	
	Vector4(float _x, float _y, float _z, float _w)
	{
		x = _x;
		y = _y;
		z = _z;
		w = _w;
	}
	Vector4 operator/ (float s) const
	{
		float rcp = 1.0f / s;
		return Vector4(x*rcp, y*rcp, z*rcp, w*rcp);
	}
};

struct Quaternion
{
	float	x;
	float	y;
	float	z;
	float	w;
	
	static  Quaternion	createRotation(float axisX, float axisY, float axisZ, float radian)
	{
		// assume axis is normalized
		float sinR= sinf(radian*0.5f);
		float cosR= cosf(radian*0.5f);
	
		float x= axisX * sinR;
		float y= axisY * sinR;
		float z= axisZ * sinR;
		float w= cosR;
	
		Quaternion q= { x, y, z, w };
		return q;
	}
	
	static Quaternion	createRotation(const Vector3& axis, float radian)
	{
		return Quaternion::createRotation(axis.x, axis.y, axis.z, radian);
	}
	
	float	norm2() const
	{
		return x*x + y*y + z*z + w*w;
	}
};

inline float matrix2x2Det(float mat[]) {
	return mat[0] * mat[3] - mat[1] * mat[2];
}

inline void matrix2x2Inverse(float *mat, float *matInv) {
	float det = matrix2x2Det(mat);
	matInv[0] = mat[3];
	matInv[1] = -mat[1];
	matInv[2] = -mat[2];
	matInv[3] = mat[0];

	det = 1 / det;
	for (int i = 0; i<4; ++i)
		matInv[i] *= det;
}

inline void matrix2x2Multiplication(float *a, float *b, float *axb) {
	axb[0] = a[0] * b[0] + a[2] * b[1];
	axb[1] = a[1] * b[0] + a[3] * b[1];
	axb[2] = a[0] * b[2] + a[2] * b[3];
	axb[3] = a[1] * b[2] + a[3] * b[3];
}

struct Matrix4x4
{
	float f[16];		// column major matrix

	static Matrix4x4 Create(float M[16]) {
		Matrix4x4 mat;
		mat.setMatrix(M);
		return mat;
	}

	static Matrix4x4 CreateRotationX(float radian) {
		Matrix4x4 M;

		for (int i = 0; i< 16; ++i)
			M.f[i] = 0;

		float cosR = cosf(radian);
		float sinR = sinf(radian);

		M.f[0] = 1;
		M.f[5] = cosR;
		M.f[6] = sinR;
		M.f[9] = -sinR;
		M.f[10] = cosR;
		M.f[15] = 1;
		return M;
	}

	static Matrix4x4 CreateRotationY(float radian){
		Matrix4x4 M;
	
		for(int i=0; i< 16; ++i)
			M.f[i]= 0;
	
		float cosR= cosf(radian);
		float sinR= sinf(radian);
	
		M.f[0]= cosR;
		M.f[2]= -sinR;
		M.f[5]= 1;
		M.f[8]= sinR;
		M.f[10]= cosR;
		M.f[15]= 1;
		return M;
	
	}
	
	static Matrix4x4 CreateRotation(const Vector3& axis, float radian)
	{
		return Matrix4x4::CreateFromQuaternion(Quaternion::createRotation(axis, radian));
	}

	static Matrix4x4 CreatePerspectiveProjection(float fov, float aspect, float _near, float _far) {
		Matrix4x4 M;

		float f, n, realAspect;

		{
			realAspect = aspect;
			f = realAspect / (float)tanf(fov * 0.5f);
		}

		n = 1.0f / (_near - _far);

		M.f[0] = f / realAspect;
		M.f[1] = 0;
		M.f[2] = 0;
		M.f[3] = 0;

		M.f[4] = 0;
		M.f[5] = f;
		M.f[6] = 0;
		M.f[7] = 0;

		M.f[8] = 0;
		M.f[9] = 0;
		M.f[10] = (_far + _near) * n;
		M.f[11] = -1;

		M.f[12] = 0;
		M.f[13] = 0;
		M.f[14] = (2 * _far * _near) * n;
		M.f[15] = 0;

		return M;
	}

	void setTranslation(const Vector3& tran) {
		f[12] = tran.x;
		f[13] = tran.y;
		f[14] = tran.z;
	}

	void setMatrix(float M[]) {
		for (int i = 0; i<16; ++i)
			f[i] = M[i];
	}
	
	static  Matrix4x4 CreateFromQuaternion(const Quaternion &q)
	{
		Matrix4x4 M;
	
		float s= 2.0f/ q.norm2();
		M.f[0]= 1- s* (q.y * q.y + q.z*q.z);
		M.f[1]= s* (q.x* q.y + q.w *q.z);
		M.f[2]= s* (q.x* q.z - q.w * q.y);
		M.f[3]= 0;
		M.f[4]= s*(q.x*q.y- q.w*q.z);
		M.f[5]= 1- s*(q.x*q.x + q.z*q.z);
		M.f[6]= s* (q.y* q.z + q.w*q.x);
		M.f[7]= 0;
		M.f[8]= s*(q.x*q.z + q.w*q.y);
		M.f[9]= s*(q.y*q.z - q.w*q.x);
		M.f[10]= 1- s*(q.x*q.x + q.y*q.y);
		M.f[11]= 0;
		M.f[12]= 0;
		M.f[13]= 0;
		M.f[14]= 0;
		M.f[15]= 1;
	
		return M;
	}

	static Matrix4x4 CreateLookAt(const Vector3& pos, const Vector3& lookAt, const Vector3& up) {
		Matrix4x4 viewMat;

		Vector3 viewDir = lookAt - pos;
		viewDir.normalize();

		Vector3 right = viewDir.cross(up);
		right.normalize();
		Vector3 upV = right.cross(viewDir);
		upV.normalize();

		float mat[] =
		{	// Rotate					*		Translate (-eye)
			right.x, upV.x, -viewDir.x,  0,
			right.y, upV.y, -viewDir.y,  0,
			right.z, upV.z, -viewDir.z,  0,
			-right.x	* pos.x - right.y	* pos.y - right.z	* pos.z,
			-upV.x		* pos.x - upV.y		* pos.y - upV.z		* pos.z,
			viewDir.x	* pos.x + viewDir.y * pos.y + viewDir.z * pos.z,
			1
		};

		viewMat.setMatrix(mat);
		return viewMat;
	}

	float determinant() const {
		return
			f[0] * f[5] * f[10] * f[15] + f[0] * f[9] * f[14] * f[7] + f[0] * f[13] * f[6] * f[11]
			+ f[4] * f[1] * f[14] * f[11] + f[4] * f[9] * f[2] * f[15] + f[4] * f[13] * f[10] * f[3]
			+ f[8] * f[1] * f[6] * f[15] + f[8] * f[5] * f[14] * f[3] + f[8] * f[13] * f[2] * f[7]
			+ f[12] * f[1] * f[10] * f[7] + f[12] * f[5] * f[2] * f[11] + f[12] * f[9] * f[6] * f[3]

			- f[0] * f[5] * f[14] * f[11] - f[0] * f[9] * f[6] * f[15] - f[0] * f[13] * f[10] * f[7]
			- f[4] * f[1] * f[10] * f[15] - f[4] * f[9] * f[14] * f[3] - f[4] * f[13] * f[2] * f[11]
			- f[8] * f[1] * f[14] * f[7] - f[8] * f[5] * f[2] * f[15] - f[8] * f[13] * f[6] * f[3]
			- f[12] * f[1] * f[6] * f[11] - f[12] * f[5] * f[10] * f[3] - f[12] * f[9] * f[2] * f[7];

	}

	Matrix4x4 inverseGeneral() const {
		float det = determinant();
		if (det == 0)
			return Matrix4x4();

		float m_inv[16];
		m_inv[0] = f[5] * f[10] * f[15] + f[9] * f[14] * f[7] + f[13] * f[6] * f[11] - f[5] * f[14] * f[11] - f[9] * f[6] * f[15] - f[13] * f[10] * f[7];
		m_inv[4] = f[4] * f[14] * f[11] + f[8] * f[6] * f[15] + f[12] * f[10] * f[7] - f[8] * f[10] * f[15] - f[8] * f[14] * f[7] - f[12] * f[6] * f[11];
		m_inv[8] = f[4] * f[9] * f[15] + f[8] * f[13] * f[7] + f[12] * f[5] * f[11] - f[4] * f[13] * f[11] - f[8] * f[5] * f[15] - f[12] * f[9] * f[7];
		m_inv[12] = f[4] * f[13] * f[10] + f[8] * f[5] * f[14] + f[12] * f[9] * f[6] - f[4] * f[9] * f[14] - f[8] * f[13] * f[6] - f[12] * f[5] * f[10];

		m_inv[1] = f[1] * f[14] * f[11] + f[9] * f[2] * f[15] + f[13] * f[10] * f[3] - f[1] * f[10] * f[15] - f[9] * f[14] * f[3] - f[13] * f[2] * f[11];
		m_inv[5] = f[0] * f[10] * f[15] + f[8] * f[14] * f[3] + f[12] * f[2] * f[11] - f[0] * f[14] * f[11] - f[8] * f[2] * f[15] - f[12] * f[10] * f[3];
		m_inv[9] = f[0] * f[13] * f[11] + f[8] * f[1] * f[15] + f[12] * f[9] * f[3] - f[0] * f[9] * f[15] - f[8] * f[13] * f[3] - f[12] * f[1] * f[11];
		m_inv[13] = f[0] * f[9] * f[14] + f[8] * f[13] * f[2] + f[12] * f[1] * f[10] - f[0] * f[13] * f[10] - f[8] * f[1] * f[14] - f[12] * f[9] * f[2];

		m_inv[2] = f[1] * f[6] * f[15] + f[5] * f[14] * f[3] + f[13] * f[2] * f[7] - f[1] * f[14] * f[7] - f[5] * f[2] * f[15] - f[13] * f[6] * f[3];
		m_inv[6] = f[0] * f[14] * f[7] + f[4] * f[2] * f[15] + f[12] * f[6] * f[3] - f[0] * f[6] * f[15] - f[4] * f[14] * f[3] - f[12] * f[2] * f[7];
		m_inv[10] = f[0] * f[5] * f[15] + f[4] * f[13] * f[3] + f[12] * f[1] * f[7] - f[0] * f[13] * f[7] - f[4] * f[1] * f[15] - f[12] * f[5] * f[3];
		m_inv[14] = f[0] * f[13] * f[6] + f[4] * f[1] * f[14] + f[12] * f[5] * f[2] - f[0] * f[5] * f[14] - f[4] * f[13] * f[2] - f[12] * f[1] * f[6];

		m_inv[3] = f[1] * f[10] * f[7] + f[5] * f[2] * f[11] + f[9] * f[6] * f[3] - f[1] * f[6] * f[11] - f[5] * f[10] * f[3] - f[9] * f[2] * f[7];
		m_inv[7] = f[0] * f[6] * f[11] + f[4] * f[10] * f[3] + f[8] * f[2] * f[7] - f[0] * f[10] * f[7] - f[4] * f[2] * f[11] - f[8] * f[6] * f[3];
		m_inv[11] = f[0] * f[9] * f[7] + f[4] * f[1] * f[11] + f[8] * f[5] * f[3] - f[0] * f[5] * f[11] - f[4] * f[9] * f[3] - f[8] * f[1] * f[7];
		m_inv[15] = f[0] * f[5] * f[10] + f[4] * f[9] * f[2] + f[8] * f[1] * f[6] - f[0] * f[9] * f[6] - f[4] * f[1] * f[10] - f[8] * f[5] * f[2];

		det = 1 / det;
		for (int i = 0; i<16; ++i)
			m_inv[i] *= det;

		return Matrix4x4::Create(m_inv);
	}

	Matrix4x4 inverse() const {
		float p[4], q[4], r[4], s[4];

		p[0] = f[0];
		p[1] = f[1];
		p[2] = f[4];
		p[3] = f[5];

		q[0] = f[8];
		q[1] = f[9];
		q[2] = f[12];
		q[3] = f[13];

		r[0] = f[2];
		r[1] = f[3];
		r[2] = f[6];
		r[3] = f[7];

		s[0] = f[10];
		s[1] = f[11];
		s[2] = f[14];
		s[3] = f[15];

		float det = matrix2x2Det(p);
		const float epsilon = 0.0001f;
		if (det >-epsilon && det <epsilon) {
			//	if ( det ==0){
			// use other general method
			return inverseGeneral();
		}

		float p_inv[4], RxPinv[4], PinvxQ[4], RxPinvxQ[4], SminusRxPinvxQ[4];
		matrix2x2Inverse(p, p_inv);

		matrix2x2Multiplication(r, p_inv, RxPinv);
		matrix2x2Multiplication(p_inv, q, PinvxQ);
		matrix2x2Multiplication(RxPinv, q, RxPinvxQ);

		for (int i = 0; i< 4; ++i)
			SminusRxPinvxQ[i] = s[i] - RxPinvxQ[i];

		det = matrix2x2Det(SminusRxPinvxQ);
		if (det >-epsilon && det <epsilon) {
			//	if (det== 0){
			// no inverse
			return Matrix4x4();
		}

		float neg_s[4], neg_r[4], neg_q[4], neg_p[4];
		matrix2x2Inverse(SminusRxPinvxQ, neg_s);

		matrix2x2Multiplication(neg_s, RxPinv, neg_r);
		for (int i = 0; i< 4; ++i)
			neg_r[i] = -neg_r[i];

		matrix2x2Multiplication(PinvxQ, neg_s, neg_q);
		for (int i = 0; i< 4; ++i)
			neg_q[i] = -neg_q[i];

		matrix2x2Multiplication(PinvxQ, neg_r, neg_p);
		for (int i = 0; i<4; ++i)
			neg_p[i] = p_inv[i] - neg_p[i];

		float m_inv[16];
		m_inv[0] = neg_p[0];
		m_inv[1] = neg_p[1];
		m_inv[2] = neg_r[0];
		m_inv[3] = neg_r[1];

		m_inv[4] = neg_p[2];
		m_inv[5] = neg_p[3];
		m_inv[6] = neg_r[2];
		m_inv[7] = neg_r[3];

		m_inv[8] = neg_q[0];
		m_inv[9] = neg_q[1];
		m_inv[10] = neg_s[0];
		m_inv[11] = neg_s[1];

		m_inv[12] = neg_q[2];
		m_inv[13] = neg_q[3];
		m_inv[14] = neg_s[2];
		m_inv[15] = neg_s[3];

		return  Matrix4x4::Create(m_inv);
	}

	Matrix4x4 operator* (const Matrix4x4& M) const {
		float mat[16];
		mat[0] = f[0] * M.f[0] + f[4] * M.f[1] + f[8] * M.f[2] + f[12] * M.f[3];
		mat[1] = f[1] * M.f[0] + f[5] * M.f[1] + f[9] * M.f[2] + f[13] * M.f[3];
		mat[2] = f[2] * M.f[0] + f[6] * M.f[1] + f[10] * M.f[2] + f[14] * M.f[3];
		mat[3] = f[3] * M.f[0] + f[7] * M.f[1] + f[11] * M.f[2] + f[15] * M.f[3];

		mat[4] = f[0] * M.f[4] + f[4] * M.f[5] + f[8] * M.f[6] + f[12] * M.f[7];
		mat[5] = f[1] * M.f[4] + f[5] * M.f[5] + f[9] * M.f[6] + f[13] * M.f[7];
		mat[6] = f[2] * M.f[4] + f[6] * M.f[5] + f[10] * M.f[6] + f[14] * M.f[7];
		mat[7] = f[3] * M.f[4] + f[7] * M.f[5] + f[11] * M.f[6] + f[15] * M.f[7];

		mat[8] = f[0] * M.f[8] + f[4] * M.f[9] + f[8] * M.f[10] + f[12] * M.f[11];
		mat[9] = f[1] * M.f[8] + f[5] * M.f[9] + f[9] * M.f[10] + f[13] * M.f[11];
		mat[10] = f[2] * M.f[8] + f[6] * M.f[9] + f[10] * M.f[10] + f[14] * M.f[11];
		mat[11] = f[3] * M.f[8] + f[7] * M.f[9] + f[11] * M.f[10] + f[15] * M.f[11];

		mat[12] = f[0] * M.f[12] + f[4] * M.f[13] + f[8] * M.f[14] + f[12] * M.f[15];
		mat[13] = f[1] * M.f[12] + f[5] * M.f[13] + f[9] * M.f[14] + f[13] * M.f[15];
		mat[14] = f[2] * M.f[12] + f[6] * M.f[13] + f[10] * M.f[14] + f[14] * M.f[15];
		mat[15] = f[3] * M.f[12] + f[7] * M.f[13] + f[11] * M.f[14] + f[15] * M.f[15];

		return Matrix4x4::Create(mat);
	}
	
	Vector4 Matrix4x4::operator* (const Vector4& v) const{

		Vector4 newV;
	
		newV.x= v.x * f[0] + v.y * f[4] + v.z* f[ 8] + v.w * f[12] ;
		newV.y= v.x * f[1] + v.y * f[5] + v.z* f[ 9] + v.w * f[13] ;
		newV.z= v.x * f[2] + v.y * f[6] + v.z* f[10] + v.w * f[14];
		newV.w= v.x * f[3] + v.y * f[7] + v.z* f[11] + v.w * f[15];
	
		return newV;
}

};
