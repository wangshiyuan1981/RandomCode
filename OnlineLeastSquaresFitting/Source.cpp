#include <stdio.h>
#include <array>

//====================================================================
template<size_t N>
using TVector = std::array<float, N>;

template<size_t M, size_t N>
using TMatrix = std::array<TVector<N>, M>;

template<size_t N>
using TSquareMatrix = TMatrix<N,N>;

// data points are 2d vectors
typedef TVector<2> TDataPoint; 
template <size_t NUMDATAPOINTS>
using TDataPointList = std::array<TDataPoint, NUMDATAPOINTS>;

//====================================================================
template <size_t N>
float DotProduct (const TVector<N>& A, const TVector<N>& B)
{
    float ret = 0.0f;
    for (size_t i = 0; i < N; ++i)
        ret += A[i] * B[i];
    return ret;
}

//====================================================================
template <size_t N>
void MinorMatrix (const TSquareMatrix<N>& in, TSquareMatrix<N-1>& out, size_t excludeI, size_t excludeJ)
{
    size_t destI = 0;
    for (size_t i = 0; i < N; ++i)
    {
        if (i != excludeI)
        {
            size_t destJ = 0;
            for (size_t j = 0; j < N; ++j)
            {
                if (j != excludeJ)
                {
                    out[destI][destJ] = in[i][j];
                    ++destJ;
                }
            }
            ++destI;
        }
    }
}

//====================================================================
template <size_t M, size_t N>
void TransposeMatrix (const TMatrix<M, N>& in, TMatrix<N, M>& result)
{
    for (size_t j = 0; j < M; ++j)
        for (size_t k = 0; k < N; ++k)
            result[k][j] = in[j][k];
}

//====================================================================
template <size_t N>
float Determinant (const TSquareMatrix<N>& in)
{
    float determinant = 0.0f;
    TSquareMatrix<N - 1> minor;
    for (size_t j = 0; j < N; ++j)
    {
        MinorMatrix(in, minor, 0, j);

        float minorDeterminant = Determinant(minor);
        if (j % 2 == 1)
            minorDeterminant *= -1.0f;

        determinant += in[0][j] * minorDeterminant;
    }
    return determinant;
}

//====================================================================
template <>
float Determinant<2> (const TSquareMatrix<2>& in)
{
    return in[0][0] * in[1][1] - in[0][1] * in[1][0];
}

//====================================================================
template <size_t N>
bool InvertMatrix (const TSquareMatrix<N>& in, TSquareMatrix<N>& out)
{
    // calculate the cofactor matrix and determinant
    float determinant = 0.0f;
    TSquareMatrix<N> cofactors;
    TSquareMatrix<N-1> minor;
    for (size_t i = 0; i < N; ++i)
    {
        for (size_t j = 0; j < N; ++j)
        {
            MinorMatrix(in, minor, i, j);

            cofactors[i][j] = Determinant(minor);
            if ((i + j) % 2 == 1)
                cofactors[i][j] *= -1.0f;

            if (i == 0)
                determinant += in[i][j] * cofactors[i][j];
        }
    }

    // matrix cant be inverted if determinant is zero
    if (determinant == 0.0f)
        return false;

    // calculate the adjoint matrix into the out matrix
    TransposeMatrix(cofactors, out);

    // divide by determinant
    float oneOverDeterminant = 1.0f / determinant;
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < N; ++j)
            out[i][j] *= oneOverDeterminant;
    return true;
}

//====================================================================
template <>
bool InvertMatrix<2> (const TSquareMatrix<2>& in, TSquareMatrix<2>& out)
{
    float determinant = Determinant(in);
    if (determinant == 0.0f)
        return false;

    float oneOverDeterminant = 1.0f / determinant;
    out[0][0] =  in[1][1] * oneOverDeterminant;
    out[0][1] = -in[0][1] * oneOverDeterminant;
    out[1][0] = -in[1][0] * oneOverDeterminant;
    out[1][1] =  in[0][0] * oneOverDeterminant;
    return true;
}

//====================================================================
constexpr size_t ATACompressedStorageSize (size_t degree)
{
    return degree == 0 ? 1 : ATACompressedStorageSize(degree - 1) * 2 + 1;
}

//====================================================================
template <size_t DEGREE>  // 1 = linear, 2 = quadratic, etc
class COnlineLeastSquaresFitter
{
public:

    template <size_t NUMDATAPOINTS>
    COnlineLeastSquaresFitter (const TDataPointList<NUMDATAPOINTS>& initialData)
    {
        static_assert(NUMDATAPOINTS >= DEGREE + 1, "There needs to be DEGREE+1 initial data points supplied to the constructor of COnlineLeastSquaresFitter.");

        // calculate matrix "A"
        // A_jk = x_j ^ k
        TMatrix<NUMDATAPOINTS, DEGREE + 1> A;
        for (size_t j = 0; j < NUMDATAPOINTS; ++j)
            for (size_t k = 0; k < DEGREE + 1; ++k)
                A[j][k] = std::pow(initialData[j][0], float(k));

        // calculate matrix "ATA"  - A transposed times A
        TMatrix<DEGREE + 1, NUMDATAPOINTS> AT;
        TransposeMatrix(A, AT);
        TMatrix<DEGREE + 1, DEGREE + 1> ATA;
        for (size_t j = 0; j < DEGREE + 1; ++j)
            for (size_t k = 0; k < DEGREE + 1; ++k)
                ATA[j][k] = DotProduct(AT[j], AT[k]);

        // Calculate vector "ATY" - A transposed times the Y values of the input data
        TMatrix<2, NUMDATAPOINTS> initialDataTransposed;
        TransposeMatrix(initialData, initialDataTransposed);
        for (size_t i = 0; i < DEGREE + 1; ++i)
            m_ATY[i] = DotProduct(AT[i], initialDataTransposed[1]);

        // Compress the ATA matrix
        CompressATAMatrix(ATA);
    }

    void AddDataPoint (const TDataPoint& dataPoint)
    {
        // Given a new data point (x_j, y_j), we need to adjust...
        // 1) ATA_ik += x_j^(i+k)
        // 2) ATY_i += x_j^i * y_j
        // where i, k is in [0, DEGREE]

        // Decompress the ATA matrix so we can update it.
        // It would be more efficient to update the compressed ATA matrix, but doing this for clarity of the code.
        TMatrix<DEGREE + 1, DEGREE + 1> ATA;
        DecompressATAMatrix(ATA);

        // 1) ATA_ik += x_j^(i+k)
        for (size_t i = 0; i < DEGREE + 1; ++i)
            for (size_t k = 0; k < DEGREE + 1; ++k)
                ATA[i][k] += std::pow(dataPoint[0], float(i+k));

        // Recompress the ATA matrix since we are done modifying it
        CompressATAMatrix(ATA);

        // 2) ATY_i += x_j^i * y_j
        for (size_t i = 0; i < DEGREE + 1; ++i)
            m_ATY[i] += std::pow(dataPoint[0], float(i)) * dataPoint[1];
    }

    bool CalculateCoefficients (TVector<DEGREE+1>& coefficients)
    {
        // Decompress the ATA matrix
        TMatrix<DEGREE + 1, DEGREE + 1> ATA;
        DecompressATAMatrix(ATA);

        // calculate inverse matrix of ATA matrix
        TMatrix<DEGREE + 1, DEGREE + 1> ATAInverse;
        if (!InvertMatrix(ATA, ATAInverse))
            return false;

        // calculate the coefficients
        for (size_t i = 0; i < DEGREE + 1; ++i)
            coefficients[i] = DotProduct(ATAInverse[i], m_ATY);

        return true;
    }

private:
    void DecompressATAMatrix (TMatrix<DEGREE + 1, DEGREE + 1>& ATA)
    {
        // ATA is symmetrical across the diagonal so we only need to store the diagonal and top half.
        // We can just restore those values and reflect the top half for the lower half.
        size_t inputIndex = 0;
        for (size_t i = DEGREE + 1; i > 0; --i)
        {
            size_t row = (DEGREE + 1) - i;
            for (size_t j = 0; j < i; ++j)
            {
                ATA[row][row + j] = m_ATACompressed[inputIndex];
                ++inputIndex;
            }
        }
        // reflect
        for (size_t i = 1; i < DEGREE + 1; ++i)
        {
            for (size_t j = 0; j < i; ++j)
                ATA[i][j] = ATA[j][i];
        }
    }

    void CompressATAMatrix (const TMatrix<DEGREE + 1, DEGREE + 1>& ATA)
    {
        // ATA is symmetrical across the diagonal so we only need to store the diagonal and top half.
        size_t outputIndex = 0;
        for (size_t i = DEGREE + 1; i > 0; --i)
        {
            size_t row = (DEGREE + 1) - i;
            for (size_t j = 0; j < i; ++j)
            {
                m_ATACompressed[outputIndex] = ATA[row][row + j];
                ++outputIndex;
            }
        }
    }

private:
    // Total storage space needed is:
    // y = 0.5*x^2 + 2.5x + 2
    // Where y is number of values that need to be stored and x is the degree of the polynomial
    TVector<DEGREE+1>                           m_ATY;
    TVector<ATACompressedStorageSize(DEGREE)>   m_ATACompressed;
};

//====================================================================
int main (int argc, char **argv)
{
    #define TEST() 0

    #if TEST() == 0
        // Fit some initial data points
        TDataPointList<4> initialData { {
            { { 1.0f, 5.0f } },
            { { 2.0f, 16.0f } },
            { { 3.0f, 31.0f } },
            { { 4.0f, 16.0f } }
        } };
        COnlineLeastSquaresFitter<3> fitter(initialData);

        // calculate coefficients
        TVector<4> coefficients;
    #endif

    #if TEST() == 1
        // Fit some initial data points
        TDataPointList<3> initialData { {
            { { 1.0f, 5.0f } },
            { { 2.0f, 16.0f } },
            { { 3.0f, 31.0f } }
        } };
        COnlineLeastSquaresFitter<2> fitter(initialData);

        // calculate coefficients
        TVector<3> coefficients;
    #endif

    #if TEST() == 2
        // Fit some initial data points
        TDataPointList<2> initialData { {
            { { 1.0f, 2.0f } },
            { { 2.0f, 4.0f } }
        } };
        COnlineLeastSquaresFitter<1> fitter(initialData);

        // Fit another value
        fitter.AddDataPoint({ 3.0f, 6.0f });

        // calculate coefficients
        TVector<2> coefficients;
    #endif


    bool success = fitter.CalculateCoefficients(coefficients);
    if (!success)
    {
        printf("ATA Matrix could not be inverted!\n");
        system("pause");
        return 1;
    }

    // print out the formula
    printf("y = ");
    for (int i = (int)coefficients.size() - 1; i >= 0; --i)
    {
        if (i < (int)coefficients.size()-1)
            printf(" + ");

        printf("%0.2f", coefficients[i]);

        if (i > 0)
            printf("x");

        if (i > 1)
            printf("^%i", i);
    }
    printf("\n");
    system("pause");
    return 0;
}

/*

TODO:
* make tests that show it working!
* improve display: hide 0 terms, don't make it show + -!

*/