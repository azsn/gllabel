// This shader slightly modified from source code by Will Dobbie.
// See dobbieText.cpp for more info.

#version 330 core
precision highp float;

#define numSS 4
#define pi 3.1415926535897932384626433832795
#define kPixelWindowSize 1.0
//insertdefines

uniform sampler2D uGridmapSampler;
uniform sampler2D uBeziermapSampler;
uniform vec2 uGridmapTexelSize;
uniform vec2 uBeziermapTexelSize;

varying vec4 vColor;
varying vec4 vGridRect;
varying vec2 vNormCoord;
varying float vBezierIndex;

float positionAt(float p0, float p1, float p2, float t) {
    float mt = 1.0 - t;
    return mt*mt*p0 + 2.0*t*mt*p1 + t*t*p2;
}

float tangentAt(float p0, float p1, float p2, float t) {
    return 2.0 * (1.0-t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

bool almostEqual(float a, float b) {
    return abs(a-b) < 1e-5;
}

float normalizedUshortFromVec2(vec2 v) {
    // produces value in (0,1) range from a vec2
    // vec2 is assumed to come from two unsigned bytes, where v.x is the most significant byte and v.y is the least significant

    // equivalent to this:
    // return (v.x * 65280.0 + vec2.y * 255.0) / 65535.0;

    return (256.0/257.0) * v.y + (1.0/257.0) * v.x;

}    

void fetchBezier(int coordIndex, out vec2 p[3]) {
    for (int i=0; i<3; i++) {
		vec4 tex = texture2D(uBeziermapSampler, vec2(coordIndex*3+i+0.5, vBezierIndex+0.5)*uBeziermapTexelSize);
		p[i] = vec2(normalizedUshortFromVec2(tex.rg), normalizedUshortFromVec2(tex.ba)) - vNormCoord;
        // p[i] = fetchVec2(vec2(vCurvesMin.x + float(coordIndex + i + 1), vCurvesMin.y)) - vNormCoord;
    }
}

int getAxisIntersections(float p0, float p1, float p2, out vec2 t) {
    if (almostEqual(p0, 2.0*p1 - p2)) {
        t[0] = 0.5 * (p2 - 2.0*p1) / (p2 - p1);
        return 1;
    }

    float sqrtTerm = p1*p1 - p0*p2;
    if (sqrtTerm < 0.0) return 0;
    sqrtTerm = sqrt(sqrtTerm);
    float denom = p0 - 2.0*p1 + p2;
    t[0] = (p0 - p1 + sqrtTerm) / denom;
    t[1] = (p0 - p1 - sqrtTerm) / denom;
    return 2;
}

float integrateWindow(float x) {
    float xsq = x*x;
    return sign(x) * (0.5 * xsq*xsq - xsq) + 0.5;           // parabolic window
    //return 0.5 * (1.0 - sign(x) * xsq);                     // box window
}

mat2 getUnitLineMatrix(vec2 b1, vec2 b2) {
    vec2 V = b2 - b1;
    float normV = length(V);
    V = V / (normV*normV);

    return mat2(V.x, -V.y, V.y, V.x);
}

void updateClosestCrossing(in vec2 porig[3], mat2 M, inout float closest, vec2 integerCell) {

	vec2 p[3];
    for (int i=0; i<3; i++) {
        p[i] = M * porig[i];
    }

    vec2 t;
    int numT = getAxisIntersections(p[0].y, p[1].y, p[2].y, t);

    for (int i=0; i<2; i++) {
        if (i == numT) break;
        if (t[i] > 0.0 && t[i] < 1.0) {
            float posx = positionAt(p[0].x, p[1].x, p[2].x, t[i]);
            vec2 op = vec2(positionAt(porig[0].x, porig[1].x, porig[2].x, t[i]),
            			   positionAt(porig[0].y, porig[1].y, porig[2].y, t[i]));
            op += vNormCoord;

            bool sameCell = floor( clamp(op * vGridRect.zw, vec2(0.5), vec2(vGridRect.zw)-0.5)) == integerCell;

            //if (posx > 0.0 && posx < 1.0 && posx < abs(closest)) {
        	if (sameCell && abs(posx) < abs(closest)) {
                float derivy = tangentAt(p[0].y, p[1].y, p[2].y, t[i]);
                closest = (derivy < 0.0) ? -posx : posx;
            }
        }
    }
}

mat2 inverse(mat2 m) {
  return mat2(m[1][1],-m[0][1],
             -m[1][0], m[0][0]) / (m[0][0]*m[1][1] - m[0][1]*m[1][0]);
}


void main()
{
    vec2 integerCell = floor( clamp(vNormCoord * vGridRect.zw, vec2(0.5), vec2(vGridRect.zw)-0.5));
    vec2 indicesCoord = vGridRect.xy + integerCell + 0.5;
    vec2 cellMid = (integerCell + 0.5) / vGridRect.zw;

    mat2 initrot = inverse(mat2(dFdx(vNormCoord) * kPixelWindowSize, dFdy(vNormCoord) * kPixelWindowSize));

    float theta = pi/float(numSS);
    mat2 rotM = mat2(cos(theta), sin(theta), -sin(theta), cos(theta));      // note this is column major ordering

    ivec4 indices1, indices2;
    indices1 = ivec4(texture2D(uGridmapSampler, indicesCoord * uGridmapTexelSize) * 255.0 + 0.5);
    // indices2 = ivec4(texture2D(uAtlasSampler, vec2(indicesCoord.x + vGridSize.x, indicesCoord.y) * uTexelSize) * 255.0 + 0.5);

    // bool moreThanFourIndices = indices1[0] < indices1[1];
	
	float midClosest = (indices1[0] > 250 || indices1[1] > 250 || indices1[2] > 250 || indices1[3] > 250) ? -2.0 : 2.0;
	
    float firstIntersection[numSS];
    for (int ss=0; ss<numSS; ss++) {
        firstIntersection[ss] = 2.0;
    }

    float percent = 0.0;

    mat2 midTransform = getUnitLineMatrix(vNormCoord, cellMid);

    for (int bezierIndex=0; bezierIndex<4; bezierIndex++) {
        int coordIndex;

        // if (bezierIndex < 4) {
            coordIndex = indices1[bezierIndex];
        // } else {
        //     if (!moreThanFourIndices) break;
        //     coordIndex = indices2[bezierIndex-4];
        // }

        if (coordIndex == 0 || coordIndex > 250) {
            continue;
        }

        vec2 p[3];
        fetchBezier(coordIndex-1, p);
		
        updateClosestCrossing(p, midTransform, midClosest, integerCell);

        // Transform p so fragment in glyph space is a unit circle
        for (int i=0; i<3; i++) {
            p[i] = initrot * p[i];
        }

        // Iterate through angles
        for (int ss=0; ss<numSS; ss++) {
            vec2 t;
            int numT = getAxisIntersections(p[0].x, p[1].x, p[2].x, t);
		
            for (int tindex=0; tindex<2; tindex++) {
                if (tindex == numT) break;
		
                if (t[tindex] > 0.0 && t[tindex] <= 1.0) {
		
                    float derivx = tangentAt(p[0].x, p[1].x, p[2].x, t[tindex]);
                    float posy = positionAt(p[0].y, p[1].y, p[2].y, t[tindex]);
		
                    if (posy > -1.0 && posy < 1.0) {
                        // Note: whether to add or subtract in the next statement is determined
                        // by which convention the path uses: moving from the bezier start to end, 
                        // is the inside to the right or left?
                        // The wrong operation will give buggy looking results, not a simple inverse.
                        float delta = integrateWindow(posy);
                        percent = percent + (derivx < 0.0 ? delta : -delta);
		
                        float intersectDist = posy + 1.0;
                        if (intersectDist < abs(firstIntersection[ss])) {
                            firstIntersection[ss] = derivx < 0.0 ? -intersectDist : intersectDist;
                        }
                    }
                }
            }
		
            if (ss+1<numSS) {
                for (int i=0; i<3; i++) {
                    p[i] = rotM * p[i];
                }
            }
        }   // ss
    }

    bool midVal = midClosest < 0.0;

    // Add contribution from rays that started inside
    for (int ss=0; ss<numSS; ss++) {
        if ((firstIntersection[ss] >= 2.0 && midVal) || (firstIntersection[ss] > 0.0 && abs(firstIntersection[ss]) < 2.0)) {
            percent = percent + 1.0 /*integrateWindow(-1.0)*/;
        }
    }

    percent = percent / float(numSS);
	gl_FragColor = vColor;
	gl_FragColor.a *= percent;
}